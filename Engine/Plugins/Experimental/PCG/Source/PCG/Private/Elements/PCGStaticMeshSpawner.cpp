// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGStaticMeshSpawner.h"

#include "PCGComponent.h"
#include "PCGManagedResource.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGActorHelpers.h"

#include "InstancePackers/PCGInstancePackerBase.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGStaticMeshSpawner)

static TAutoConsoleVariable<bool> CVarAllowISMReuse(
	TEXT("pcg.ISM.AllowReuse"),
	true,
	TEXT("Controls whether ISMs can be reused and skipped when re-executing"));

UPCGStaticMeshSpawnerSettings::UPCGStaticMeshSpawnerSettings(const FObjectInitializer &ObjectInitializer)
{
	bUseSeed = true;

	MeshSelectorType = UPCGMeshSelectorWeighted::StaticClass();
	// Implementation note: this should not have been done here (it should have been null), as it causes issues with copy & paste
	// when the thing to paste does not have that class for its instance.
	// However, removing it makes it that any object actually using the instance created by default would be lost.
	if (!this->HasAnyFlags(RF_ClassDefaultObject))
	{
		MeshSelectorInstance = ObjectInitializer.CreateDefaultSubobject<UPCGMeshSelectorWeighted>(this, TEXT("DefaultSelectorInstance"));
	}
}

FPCGElementPtr UPCGStaticMeshSpawnerSettings::CreateElement() const
{
	return MakeShared<FPCGStaticMeshSpawnerElement>();
}

bool FPCGStaticMeshSpawnerElement::PrepareDataInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::PrepareDataInternal);
	// TODO : time-sliced implementation
	FPCGStaticMeshSpawnerContext* Context = static_cast<FPCGStaticMeshSpawnerContext*>(InContext);
	const UPCGStaticMeshSpawnerSettings* Settings = Context->GetInputSettings<UPCGStaticMeshSpawnerSettings>();
	check(Settings);

	if (!Settings->MeshSelectorInstance)
	{
		PCGE_LOG(Error, "Invalid MeshSelectorInstance");
		return true;
	}

	if (!Context->SourceComponent.Get())
	{
		return true;
	}

#if WITH_EDITOR
	// In editor, we always want to generate this data for inspection & to prevent caching issues
	const bool bGenerateOutput = true;
#else
	const bool bGenerateOutput = Context->Node && Context->Node->IsOutputPinConnected(PCGPinConstants::DefaultOutputLabel);
#endif

	// Check if we can reuse existing resources
	bool bSkippedDueToReuse = false;

	if (Context->CurrentInputIndex == 0 && CVarAllowISMReuse.GetValueOnAnyThread())
	{
		// Compute CRC if it has not been computed (it likely isn't, but this is to futureproof this)
		if (!Context->DependenciesCrc.IsValid())
		{
			GetDependenciesCrc(Context->InputData, Settings, Context->SourceComponent.Get(), Context->DependenciesCrc);
		}
		
		if (Context->DependenciesCrc.IsValid())
		{
			TArray<UPCGManagedISMComponent*> MISMCs;
			Context->SourceComponent->ForEachManagedResource([&MISMCs, &Context](UPCGManagedResource* InResource)
			{
				if (UPCGManagedISMComponent* Resource = Cast<UPCGManagedISMComponent>(InResource))
				{
					if (Resource->GetCrc().IsValid() && Resource->GetCrc() == Context->DependenciesCrc)
					{
						MISMCs.Add(Resource);
					}
				}
			});

			for (UPCGManagedISMComponent* MISMC : MISMCs)
			{
				MISMC->MarkAsReused();
			}

			if (!MISMCs.IsEmpty())
			{
				bSkippedDueToReuse = true;
			}
		}
	}

	// Early out - if we've established we could reuse resources and there is no need to generate an output, quit now
	if (!bGenerateOutput && bSkippedDueToReuse)
	{
		return true;
	}

	// perform mesh selection
	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	while(Context->CurrentInputIndex < Inputs.Num())
	{
		const FPCGTaggedData& Input = Inputs[Context->CurrentInputIndex];
		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

		// Preincrement so we can have all paths continue properly - note that this works because none of this is internally time-sliced
		++Context->CurrentInputIndex;

		if (!SpatialData)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		const UPCGPointData* PointData = SpatialData->ToPointData(Context);
		if (!PointData)
		{
			PCGE_LOG(Error, "Unable to get point data from input");
			continue;
		}

		AActor* TargetActor = Context->GetTargetActor(PointData);
		if (!TargetActor)
		{
			PCGE_LOG(Error, "Invalid target actor");
			continue;
		}

		UPCGPointData* OutputPointData = nullptr;

		if (bGenerateOutput)
		{
			FPCGTaggedData& Output = Outputs.Add_GetRef(Input);

			OutputPointData = NewObject<UPCGPointData>();
			OutputPointData->InitializeFromData(PointData);

			if (OutputPointData->Metadata->HasAttribute(Settings->OutAttributeName))
			{
				OutputPointData->Metadata->DeleteAttribute(Settings->OutAttributeName);
				PCGE_LOG(Verbose, "Metadata attribute %s is being overwritten in the output data", *Settings->OutAttributeName.ToString());
			}

			OutputPointData->Metadata->CreateStringAttribute(Settings->OutAttributeName, FName(NAME_None).ToString(), /*bAllowsInterpolation=*/false);

			Output.Data = OutputPointData;
		}

		TArray<FPCGMeshInstanceList> MeshInstances;
		Settings->MeshSelectorInstance->SelectInstances(*Context, Settings, PointData, MeshInstances, OutputPointData);

		// If we need the output but would otherwise skip the resource creation, just don't push them to the MeshInstancesData array
		if (bSkippedDueToReuse)
		{
			continue;
		}

		TArray<FPCGPackedCustomData> PackedCustomData;
		PackedCustomData.SetNum(MeshInstances.Num());
		if (Settings->InstancePackerInstance)
		{
			for(int32 InstanceListIndex = 0; InstanceListIndex < MeshInstances.Num(); ++InstanceListIndex)
			{
				Settings->InstancePackerInstance->PackInstances(*Context, PointData, MeshInstances[InstanceListIndex], PackedCustomData[InstanceListIndex]);
			}
		}

		FPCGStaticMeshSpawnerContext::FPackedInstanceListData& InstanceListData = Context->MeshInstancesData.Emplace_GetRef();
		InstanceListData.TargetActor = TargetActor;
		InstanceListData.SpatialData = PointData;
		InstanceListData.MeshInstances = MoveTemp(MeshInstances);
		InstanceListData.PackedCustomData = MoveTemp(PackedCustomData);

		// Continue on to next iteration if there is time left, otherwise, exit here
		if (Context->ShouldStop() && Context->CurrentInputIndex < Inputs.Num())
		{
			return false;
		}
	}

	return true;
}

bool FPCGStaticMeshSpawnerElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute);
	FPCGStaticMeshSpawnerContext* Context = static_cast<FPCGStaticMeshSpawnerContext*>(InContext);
	const UPCGStaticMeshSpawnerSettings* Settings = Context->GetInputSettings<UPCGStaticMeshSpawnerSettings>();
	check(Settings);

	while(!Context->MeshInstancesData.IsEmpty())
	{
		const FPCGStaticMeshSpawnerContext::FPackedInstanceListData& InstanceList = Context->MeshInstancesData.Last();
		check(InstanceList.MeshInstances.Num() == InstanceList.PackedCustomData.Num());

		const bool bTargetActorValid = (InstanceList.TargetActor && IsValid(InstanceList.TargetActor));

		if (bTargetActorValid)
		{
			while (Context->CurrentDataIndex < InstanceList.MeshInstances.Num())
			{
				SpawnStaticMeshInstances(Context, InstanceList.MeshInstances[Context->CurrentDataIndex], InstanceList.TargetActor, InstanceList.PackedCustomData[Context->CurrentDataIndex]);
				++Context->CurrentDataIndex;

				if (Context->ShouldStop())
				{
					break;
				}
			}
		}

		if (!bTargetActorValid || Context->CurrentDataIndex == InstanceList.MeshInstances.Num())
		{
			Context->MeshInstancesData.RemoveAtSwap(Context->MeshInstancesData.Num() - 1);
			Context->CurrentDataIndex = 0;
		}

		if (Context->ShouldStop())
		{
			break;
		}
	}

	return Context->MeshInstancesData.IsEmpty();
}

FPCGContext* FPCGStaticMeshSpawnerElement::Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node)
{
	FPCGStaticMeshSpawnerContext* Context = new FPCGStaticMeshSpawnerContext();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	return Context;
}

bool FPCGStaticMeshSpawnerElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	return Context->CurrentPhase == EPCGExecutionPhase::Execute;
}

void FPCGStaticMeshSpawnerElement::SpawnStaticMeshInstances(FPCGContext* Context, const FPCGMeshInstanceList& InstanceList, AActor* TargetActor, const FPCGPackedCustomData& PackedCustomData) const
{
	// Populate the (H)ISM from the previously prepared entries
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::PopulateISMs);

	if (InstanceList.Instances.Num() == 0)
	{
		return;
	}

	// TODO: we could likely pre-load these meshes asynchronously in the settings
	UStaticMesh* LoadedMesh = InstanceList.Descriptor.StaticMesh.LoadSynchronous();

	if (!LoadedMesh)
	{
		// Either we have no mesh (so nothing to do) or the mesh couldn't be loaded
		if (InstanceList.Descriptor.StaticMesh.IsValid())
		{
			PCGE_LOG(Error, "Unable to load mesh %s", *InstanceList.Descriptor.StaticMesh.ToString());
		}

		return;
	}

	// TODO: we could likely pre-load these materials asynchronously in the settings
	for (TSoftObjectPtr<UMaterialInterface> OverrideMaterial : InstanceList.Descriptor.OverrideMaterials)
	{
		if (OverrideMaterial.IsValid() && !OverrideMaterial.LoadSynchronous())
		{
			PCGE_LOG(Error, "Unable to load override material %s", *OverrideMaterial.ToString());
			return;
		}
	}

	FPCGISMCBuilderParameters Params;
	Params.Descriptor = FISMComponentDescriptor(InstanceList.Descriptor);
	Params.NumCustomDataFloats = PackedCustomData.NumCustomDataFloats;

	// If the root actor we're binding to is movable, then the ISMC should be movable by default
	if (USceneComponent* SceneComponent = TargetActor->GetRootComponent())
	{
		Params.Descriptor.Mobility = SceneComponent->Mobility;
	}

	UPCGManagedISMComponent* MISMC = UPCGActorHelpers::GetOrCreateManagedISMC(TargetActor, Context->SourceComponent.Get(), Params);
	
	check(MISMC);
	MISMC->SetCrc(Context->DependenciesCrc);

	UInstancedStaticMeshComponent* ISMC = MISMC->GetComponent();
	check(ISMC);

	const int32 PreExistingInstanceCount = ISMC->GetInstanceCount();
	const int32 NewInstanceCount = InstanceList.Instances.Num();
	const int32 NumCustomDataFloats = PackedCustomData.NumCustomDataFloats;

	check((ISMC->NumCustomDataFloats == 0 && PreExistingInstanceCount == 0) || ISMC->NumCustomDataFloats == NumCustomDataFloats);
	ISMC->NumCustomDataFloats = NumCustomDataFloats;

	// The index in ISMC PerInstanceSMCustomData where we should pick up to begin inserting new floats
	const int32 PreviousCustomDataOffset = PreExistingInstanceCount * NumCustomDataFloats;

	// Populate the ISM instances
	ISMC->AddInstances(InstanceList.Instances, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);

	// Copy new CustomData into the ISMC PerInstanceSMCustomData
	if (NumCustomDataFloats > 0)
	{
		check(PreviousCustomDataOffset + PackedCustomData.CustomData.Num() == ISMC->PerInstanceSMCustomData.Num());
		FMemory::Memcpy(&ISMC->PerInstanceSMCustomData[PreviousCustomDataOffset], &PackedCustomData.CustomData[0], PackedCustomData.CustomData.Num() * sizeof(float));

		// Force recreation of the render data when proxy is created
		ISMC->InstanceUpdateCmdBuffer.NumEdits++;
	}

	ISMC->UpdateBounds();

	PCGE_LOG(Verbose, "Added %d instances of %s on actor %s", InstanceList.Instances.Num(), *InstanceList.Descriptor.StaticMesh->GetFName().ToString(), *TargetActor->GetFName().ToString());
}

void UPCGStaticMeshSpawnerSettings::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (Meshes_DEPRECATED.Num() != 0)
	{
		SetMeshSelectorType(UPCGMeshSelectorWeighted::StaticClass());

		UPCGMeshSelectorWeighted* MeshSelector = CastChecked<UPCGMeshSelectorWeighted>(MeshSelectorInstance);

		for (const FPCGStaticMeshSpawnerEntry& Entry : Meshes_DEPRECATED)
		{
			FPCGMeshSelectorWeightedEntry& NewEntry = MeshSelector->MeshEntries.Emplace_GetRef(Entry.Mesh, Entry.Weight);
			if (Entry.bOverrideCollisionProfile)
			{
				NewEntry.Descriptor.BodyInstance.SetCollisionProfileName(Entry.CollisionProfile.Name);
			}
		}

		Meshes_DEPRECATED.Reset();
	}
#endif

	const EObjectFlags Flags = GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional;
	
	if (!MeshSelectorInstance)
	{
		RefreshMeshSelector();
	}
	else
	{
		MeshSelectorInstance->SetFlags(Flags);
	}

	if (!InstancePackerInstance)
	{
		RefreshInstancePacker();
	}
	else
	{
		InstancePackerInstance->SetFlags(Flags);
	}
}

#if WITH_EDITOR
void UPCGStaticMeshSpawnerSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) 
{
	if (PropertyChangedEvent.Property)
	{
		const FName& PropertyName = PropertyChangedEvent.Property->GetFName();

		if (PropertyName == GET_MEMBER_NAME_CHECKED(UPCGStaticMeshSpawnerSettings, MeshSelectorType))
		{
			RefreshMeshSelector();
		} 
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UPCGStaticMeshSpawnerSettings, InstancePackerType))
		{
			RefreshInstancePacker();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UPCGStaticMeshSpawnerSettings::SetMeshSelectorType(TSubclassOf<UPCGMeshSelectorBase> InMeshSelectorType) 
{
	if (!MeshSelectorInstance || InMeshSelectorType != MeshSelectorType)
	{
		if (InMeshSelectorType != MeshSelectorType)
		{
			MeshSelectorType = InMeshSelectorType;
		}
		
		RefreshMeshSelector();
	}
}

void UPCGStaticMeshSpawnerSettings::SetInstancePackerType(TSubclassOf<UPCGInstancePackerBase> InInstancePackerType) 
{
	if (!InstancePackerInstance || InInstancePackerType != InstancePackerType)
	{
		if (InInstancePackerType != InstancePackerType)
		{
			InstancePackerType = InInstancePackerType;
		}
		
		RefreshInstancePacker();
	}
}

void UPCGStaticMeshSpawnerSettings::RefreshMeshSelector()
{
	if (MeshSelectorType)
	{
		if (MeshSelectorInstance)
		{
			MeshSelectorInstance->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
			MeshSelectorInstance->MarkAsGarbage();
			MeshSelectorInstance = nullptr;
		}

		const EObjectFlags Flags = GetMaskedFlags(RF_PropagateToSubObjects);
		MeshSelectorInstance = NewObject<UPCGMeshSelectorBase>(this, MeshSelectorType, NAME_None, Flags);
	}
	else
	{
		MeshSelectorInstance = nullptr;
	}
}

void UPCGStaticMeshSpawnerSettings::RefreshInstancePacker()
{
	if (InstancePackerType)
	{
		if (InstancePackerInstance)
		{
			InstancePackerInstance->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
			InstancePackerInstance->MarkAsGarbage();
			InstancePackerInstance = nullptr;
		}

		const EObjectFlags Flags = GetMaskedFlags(RF_PropagateToSubObjects);
		InstancePackerInstance = NewObject<UPCGInstancePackerBase>(this, InstancePackerType, NAME_None, Flags);
	}
	else
	{
		InstancePackerInstance = nullptr;
	}
}

FPCGStaticMeshSpawnerContext::FPackedInstanceListData::FPackedInstanceListData() = default;
FPCGStaticMeshSpawnerContext::FPackedInstanceListData::~FPackedInstanceListData() = default;
