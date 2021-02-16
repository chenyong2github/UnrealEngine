// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageISMActor.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "FoliageType_Actor.h"
#include "FoliageHelper.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "ISMPartition/ISMComponentDescriptor.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "Math/Box.h"

void FFoliageISMActor::Serialize(FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	Ar << Guid;
	ClientHandle.Serialize(Ar);
	Ar << ISMDefinition;
	Ar << ActorClass;
#endif
}

#if WITH_EDITOR
void FFoliageISMActor::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ActorClass, InThis);
}

bool FFoliageISMActor::IsInitialized() const
{
	return ClientHandle.IsValid();
}

void InitDescriptorFromFoliageType(FISMComponentDescriptor& Descriptor, const UFoliageType* FoliageType)
{
	const UFoliageType_Actor* FoliageTypeActor = Cast<UFoliageType_Actor>(FoliageType);
	Descriptor.ComponentClass = FoliageTypeActor->StaticMeshOnlyComponentClass != nullptr ? FoliageTypeActor->StaticMeshOnlyComponentClass.Get() : UFoliageInstancedStaticMeshComponent::StaticClass();
	
	Descriptor.Mobility = FoliageType->Mobility;
	Descriptor.InstanceStartCullDistance = FoliageType->CullDistance.Min;
	Descriptor.InstanceEndCullDistance = FoliageType->CullDistance.Max;
	Descriptor.bCastShadow = FoliageType->CastShadow;
	Descriptor.bCastDynamicShadow = FoliageType->bCastDynamicShadow;
	Descriptor.bCastStaticShadow = FoliageType->bCastStaticShadow;
	Descriptor.bCastContactShadow = FoliageType->bCastContactShadow;
	Descriptor.RuntimeVirtualTextures = FoliageType->RuntimeVirtualTextures;
	Descriptor.VirtualTextureRenderPassType = FoliageType->VirtualTextureRenderPassType;
	Descriptor.VirtualTextureCullMips = FoliageType->VirtualTextureCullMips;
	Descriptor.TranslucencySortPriority = FoliageType->TranslucencySortPriority;
	Descriptor.bAffectDynamicIndirectLighting = FoliageType->bAffectDynamicIndirectLighting;
	Descriptor.bAffectDistanceFieldLighting = FoliageType->bAffectDistanceFieldLighting;
	Descriptor.bCastShadowAsTwoSided = FoliageType->bCastShadowAsTwoSided;
	Descriptor.bReceivesDecals = FoliageType->bReceivesDecals;
	Descriptor.bOverrideLightMapRes = FoliageType->bOverrideLightMapRes;
	Descriptor.OverriddenLightMapRes = FoliageType->OverriddenLightMapRes;
	Descriptor.LightmapType = FoliageType->LightmapType;
	Descriptor.bUseAsOccluder = FoliageType->bUseAsOccluder;
	Descriptor.bEnableDensityScaling = FoliageType->bEnableDensityScaling;
	Descriptor.LightingChannels = FoliageType->LightingChannels;
	Descriptor.bRenderCustomDepth = FoliageType->bRenderCustomDepth;
	Descriptor.CustomDepthStencilWriteMask = FoliageType->CustomDepthStencilWriteMask;
	Descriptor.CustomDepthStencilValue = FoliageType->CustomDepthStencilValue;
	Descriptor.bIncludeInHLOD = FoliageType->bIncludeInHLOD;
	Descriptor.BodyInstance.CopyBodyInstancePropertiesFrom(&FoliageType->BodyInstance);

	Descriptor.bHasCustomNavigableGeometry = FoliageType->CustomNavigableGeometry;
	Descriptor.bEnableDiscardOnLoad = FoliageType->bEnableDiscardOnLoad;
}

void FFoliageISMActor::Initialize(const UFoliageType* FoliageType)
{
	check(!IsInitialized());

	AInstancedFoliageActor* IFA = GetIFA();

	FoliageTypeActor = Cast<const UFoliageType_Actor>(FoliageType);
	ActorClass = FoliageTypeActor->ActorClass ? FoliageTypeActor->ActorClass.Get() : AActor::StaticClass();
	
	FActorSpawnParameters SpawnParams;
	SpawnParams.bCreateActorPackage = false;
	SpawnParams.bNoFail = true;
	SpawnParams.bHideFromSceneOutliner = true;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.ObjectFlags &= ~RF_Transactional;
	SpawnParams.ObjectFlags |= RF_Transient;
	AActor* SpawnedActor = IFA->GetWorld()->SpawnActor<AActor>(ActorClass, SpawnParams);
	FTransform ActorTransform = SpawnedActor->GetActorTransform();
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	SpawnedActor->GetComponents<UStaticMeshComponent>(StaticMeshComponents);

	ClientHandle = IFA->RegisterClient(Guid);

	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		FISMComponentDescriptor Descriptor;
		// Avoid initializing the body instance as we are going to do it in the InitDescriptorFromFoliageType and that Copy of BodyInstance on a registered components will fail.
		Descriptor.InitFrom(StaticMeshComponent, /*bInitBodyInstance*/ false);
		InitDescriptorFromFoliageType(Descriptor, FoliageTypeActor);
		Descriptor.ComputeHash();

		int32 DescriptorIndex = IFA->RegisterISMComponentDescriptor(Descriptor);
		TArray<FTransform>& Transforms = ISMDefinition.FindOrAdd(DescriptorIndex);
		if (UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(StaticMeshComponent))
		{
			for (int32 InstanceIndex = 0; InstanceIndex < ISMComponent->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (ensure(ISMComponent->GetInstanceTransform(InstanceIndex, InstanceTransform, /*bWorldSpace=*/ true)))
				{
					FTransform LocalTransform = InstanceTransform.GetRelativeTransform(ActorTransform);
					Transforms.Add(LocalTransform);
				}
			}
		}
		else
		{
			FTransform LocalTransform = StaticMeshComponent->GetComponentTransform().GetRelativeTransform(ActorTransform);
			Transforms.Add(LocalTransform);
		}
	}

	IFA->GetWorld()->DestroyActor(SpawnedActor);
	
	RegisterDelegates();
}

void FFoliageISMActor::Uninitialize()
{
	check(IsInitialized());
	UnregisterDelegates();
	GetIFA()->UnregisterClient(ClientHandle);
	ISMDefinition.Empty();
}

void FFoliageISMActor::RegisterDelegates()
{
	if (ActorClass)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(ActorClass->ClassGeneratedBy))
		{
			Blueprint->OnCompiled().AddRaw(this, &FFoliageISMActor::OnBlueprintChanged);
		}
	}
}

void FFoliageISMActor::UnregisterDelegates()
{
	if (ActorClass)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(ActorClass->ClassGeneratedBy))
		{
			Blueprint->OnCompiled().RemoveAll(this);
		}
	}
}

void FFoliageISMActor::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	Reapply(FoliageTypeActor);
}

void FFoliageISMActor::Reapply(const UFoliageType* FoliageType)
{
	if (IsInitialized())
	{
		Uninitialize();
	}
	Initialize(FoliageType);
	check(IsInitialized());
	
	BeginUpdate();
	for (const FFoliageInstance& Instance : Info->Instances)
	{
		AddInstance(Instance);
	}
	EndUpdate();
}

int32 FFoliageISMActor::GetInstanceCount() const
{
	return Info->Instances.Num();
}

void FFoliageISMActor::PreAddInstances(const UFoliageType* FoliageType, int32 AddedInstanceCount)
{
	if (!IsInitialized())
	{
		Initialize(FoliageType);
		check(IsInitialized());
	}

	GetIFA()->ReserveISMInstances(ClientHandle, AddedInstanceCount, ISMDefinition);
}

void FFoliageISMActor::AddInstance(const FFoliageInstance& NewInstance)
{
	GetIFA()->AddISMInstance(ClientHandle, NewInstance.GetInstanceWorldTransform(), ISMDefinition);
}

void FFoliageISMActor::RemoveInstance(int32 InstanceIndex)
{
	bool bOutIsEmpty = false;
	GetIFA()->RemoveISMInstance(ClientHandle, InstanceIndex, &bOutIsEmpty);
	
	if(bOutIsEmpty)
	{
		Uninitialize();
	}
}

void FFoliageISMActor::BeginUpdate()
{
	GetIFA()->BeginUpdate();
}

void FFoliageISMActor::EndUpdate()
{
	GetIFA()->EndUpdate();
}

void FFoliageISMActor::SetInstanceWorldTransform(int32 InstanceIndex, const FTransform& Transform, bool bTeleport)
{
	GetIFA()->SetISMInstanceTransform(ClientHandle, InstanceIndex, Transform, bTeleport, ISMDefinition);
}

FTransform FFoliageISMActor::GetInstanceWorldTransform(int32 InstanceIndex) const
{
	return Info->Instances[InstanceIndex].GetInstanceWorldTransform();
}

bool FFoliageISMActor::IsOwnedComponent(const UPrimitiveComponent* Component) const
{
	return GetIFA()->IsISMComponent(Component);
}

void FFoliageISMActor::Refresh(bool bAsync, bool bForce)
{
	GetIFA()->UpdateHISMTrees(bAsync, bForce);
}

void FFoliageISMActor::OnHiddenEditorViewMaskChanged(uint64 InHiddenEditorViews)
{
	if (!IsInitialized())
	{
		return;
	}

	// This can give weird results if toggling the visibility of 2 foliage types that share the same meshes. The last one wins for now.
	TArray<UInstancedStaticMeshComponent*> ClientComponents;
	GetIFA()->GetClientComponents(ClientHandle, ClientComponents);
	for (UInstancedStaticMeshComponent* Component : ClientComponents)
	{
		if (UFoliageInstancedStaticMeshComponent* FoliageComponent = Cast<UFoliageInstancedStaticMeshComponent>(Component))
		{
			FoliageComponent->FoliageHiddenEditorViews = InHiddenEditorViews;
			FoliageComponent->MarkRenderStateDirty();
		}
	}
}

void FFoliageISMActor::PostEditUndo(FFoliageInfo* InInfo, UFoliageType* FoliageType)
{
	FFoliageImpl::PostEditUndo(InInfo, FoliageType);
}

void FFoliageISMActor::NotifyFoliageTypeWillChange(UFoliageType* FoliageType)
{
	UnregisterDelegates();
}

void FFoliageISMActor::NotifyFoliageTypeChanged(UFoliageType* FoliageType, bool bSourceChanged)
{
	if (!IsInitialized())
	{
		return;
	}
		
	if (UFoliageType_Actor* InFoliageTypeActor = Cast<UFoliageType_Actor>(FoliageType))
	{
		// Implementation should change
		if (!InFoliageTypeActor->bStaticMeshOnly)
		{
			Uninitialize();
			return;
		}
	}

	AInstancedFoliageActor* IFA = GetIFA();

	// Go through descriptors and see if they changed
	if (!bSourceChanged)
	{
		for (const auto& Pair : ISMDefinition)
		{
			const FISMComponentDescriptor& RegisteredDescriptor = IFA->GetISMComponentDescriptor(Pair.Key);
			FISMComponentDescriptor NewDescriptor(RegisteredDescriptor);
			InitDescriptorFromFoliageType(NewDescriptor, FoliageType);
			NewDescriptor.ComputeHash();

			if (RegisteredDescriptor != NewDescriptor)
			{
				bSourceChanged = true;
				break;
			}
		}
	}

	if (bSourceChanged)
	{
		Reapply(FoliageType);
		ApplySelection(true, Info->SelectedIndices);
	}
	else
	{
		RegisterDelegates();
	}
}

void FFoliageISMActor::SelectAllInstances(bool bSelect)
{
	TSet<int32> Indices;
	Indices.Reserve(Info->Instances.Num());
	for (int32 i = 0; i < Info->Instances.Num(); ++i)
	{
		Indices.Add(i);
	}
	SelectInstances(bSelect, Indices);
}

void FFoliageISMActor::SelectInstance(bool bSelect, int32 Index)
{
	SelectInstances(bSelect, { Index });
}

void FFoliageISMActor::SelectInstances(bool bSelect, const TSet<int32>& SelectedIndices)
{
	GetIFA()->SelectISMInstances(ClientHandle, bSelect, SelectedIndices);
}

int32 FFoliageISMActor::GetInstanceIndexFrom(const UPrimitiveComponent* PrimitiveComponent, int32 ComponentIndex) const
{
	if (IsInitialized() && ComponentIndex != INDEX_NONE)
	{
		if (const UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(PrimitiveComponent))
		{
			return GetIFA()->GetISMInstanceIndex(ClientHandle, ISMComponent, ComponentIndex);
		}
	}
	return INDEX_NONE;
}

FBox FFoliageISMActor::GetSelectionBoundingBox(const TSet<int32>& SelectedIndices) const
{
	return GetIFA()->GetISMInstanceBounds(ClientHandle, SelectedIndices);
}
void FFoliageISMActor::ApplySelection(bool bApply, const TSet<int32>& SelectedIndices)
{
	// Going in and out of Folaige with an Empty/Unregistered impl.
	if (!IsInitialized())
	{
		return;
	}

	SelectAllInstances(false);
	if (bApply)
	{
		SelectInstances(true, SelectedIndices);
	}
}

void FFoliageISMActor::ClearSelection(const TSet<int32>& SelectedIndices)
{
	SelectAllInstances(false);
}

#endif // WITH_EDITOR
