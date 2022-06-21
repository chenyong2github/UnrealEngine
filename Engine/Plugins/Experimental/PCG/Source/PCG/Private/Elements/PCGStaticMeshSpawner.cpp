// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGStaticMeshSpawner.h"

#include "PCGCommon.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGActorHelpers.h"
#include "MeshSelectors/PCGMeshSelectorBase.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

UPCGStaticMeshSpawnerSettings::UPCGStaticMeshSpawnerSettings(const FObjectInitializer &ObjectInitializer)
{
	MeshSelectorType = UPCGMeshSelectorWeighted::StaticClass();
	MeshSelectorInstance = ObjectInitializer.CreateDefaultSubobject<UPCGMeshSelectorWeighted>(this, TEXT("DefaultSelectorInstance"));
}

FPCGElementPtr UPCGStaticMeshSpawnerSettings::CreateElement() const
{
	return MakeShared<FPCGStaticMeshSpawnerElement>();
}

bool FPCGStaticMeshSpawnerElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute);
	// TODO : time-sliced implementation
	const UPCGStaticMeshSpawnerSettings* Settings = Context->GetInputSettings<UPCGStaticMeshSpawnerSettings>();
	check(Settings);

	if (!Settings->MeshSelectorInstance)
	{
		PCGE_LOG(Error, "Invalid MeshSelectorInstance");
		return true;
	}

	// perform mesh selection
	TArray<FPCGMeshInstanceList> MeshInstances;
	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	// Forward any non-input data
	Outputs.Append(Context->InputData.GetAllSettings());

	const bool bOutputPinConnected = Context->Node && Context->Node->IsOutputPinConnected(PCGPinConstants::DefaultOutputLabel);

	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Input.Data);

		if (!SpatialData)
		{
			PCGE_LOG(Error, "Invalid input data");
			continue;
		}

		AActor* TargetActor = SpatialData->TargetActor;

		if (!TargetActor)
		{
			PCGE_LOG(Error, "Invalid target actor");
			continue;
		}

		UPCGPointData* OutputPointData = nullptr;

		if (bOutputPinConnected || Settings->bForceConnectOutput)
		{
			FPCGTaggedData& Output = Outputs.Add_GetRef(Input); 
			
			OutputPointData = NewObject<UPCGPointData>();
			OutputPointData->InitializeFromData(SpatialData);

			if (OutputPointData->Metadata->HasAttribute(Settings->OutAttributeName))
			{
				OutputPointData->Metadata->DeleteAttribute(Settings->OutAttributeName);
				PCGE_LOG(Verbose, "Metadata attribute %s is being overwritten in the output data", *Settings->OutAttributeName.ToString());
			}

			OutputPointData->Metadata->CreateStringAttribute(Settings->OutAttributeName, FName(NAME_None).ToString(), false);

			Output.Data = OutputPointData;
		}

		Settings->MeshSelectorInstance->SelectInstances(*Context, Settings, SpatialData, MeshInstances, OutputPointData);

		// Spawn a static mesh for each instance
		SpawnStaticMeshInstances(Context, MeshInstances, TargetActor);

		MeshInstances.Reset();
	}

	return true;
}

void FPCGStaticMeshSpawnerElement::SpawnStaticMeshInstances(FPCGContext* Context, const TArray<FPCGMeshInstanceList>& MeshInstances, AActor* TargetActor) const
{
	// Populate the (H)ISM from the previously prepared entries
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGStaticMeshSpawnerElement::Execute::PopulateISMs);

	for (const FPCGMeshInstanceList& InstanceList : MeshInstances)
	{
		if (InstanceList.Instances.Num() == 0)
		{
			continue;
		}

		// Todo: we could likely pre-load these meshes asynchronously in the settings
		UStaticMesh* LoadedMesh = InstanceList.Mesh.LoadSynchronous();

		if (!LoadedMesh)
		{
			continue;
		}

		FPCGISMCBuilderParameters Params;
		Params.Mesh = LoadedMesh;

		if (InstanceList.bOverrideCollisionProfile)
		{
			Params.CollisionProfile = InstanceList.CollisionProfile.Name;
		}

		if (InstanceList.bOverrideMaterials)
		{
			Params.MaterialOverrides = InstanceList.MaterialOverrides;
		}

		UInstancedStaticMeshComponent* ISMC = UPCGActorHelpers::GetOrCreateISMC(TargetActor, Context->SourceComponent, Params);

		// TODO: add scaling
		// TODO: document these arguments
		ISMC->NumCustomDataFloats = 0;
		ISMC->AddInstances(InstanceList.Instances, false, true);
		ISMC->UpdateBounds();

		PCGE_LOG(Verbose, "Added %d instances of %s on actor %s", InstanceList.Instances.Num(), *InstanceList.Mesh->GetFName().ToString(), *TargetActor->GetFName().ToString());
	}
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
			NewEntry.CollisionProfile = Entry.CollisionProfile;
			NewEntry.bOverrideCollisionProfile = Entry.bOverrideCollisionProfile;
		}

		Meshes_DEPRECATED.Reset();
	}
#endif

	if (!MeshSelectorInstance)
	{
		RefreshMeshSelector();
	}
}

#if WITH_EDITOR
void UPCGStaticMeshSpawnerSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) 
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGStaticMeshSpawnerSettings, MeshSelectorType))
	{
		RefreshMeshSelector();
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

void UPCGStaticMeshSpawnerSettings::RefreshMeshSelector()
{
	if (MeshSelectorType)
	{
		MeshSelectorInstance = NewObject<UPCGMeshSelectorBase>(this, MeshSelectorType);
	}
	else
	{
		MeshSelectorInstance = nullptr;
	}
}
