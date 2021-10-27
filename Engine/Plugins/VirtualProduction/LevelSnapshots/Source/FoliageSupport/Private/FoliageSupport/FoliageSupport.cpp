// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoliageSupport/FoliageSupport.h"

#include "FoliageSupport/InstancedFoliageActorData.h"
#include "PropertySelection.h"
#include "PropertySelectionMap.h"
#include "Params/ObjectSnapshotSerializationData.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "FoliageHelper.h"
#include "ILevelSnapshotsModule.h"
#include "InstancedFoliageActor.h"
#include "ActorPartition/ActorPartitionSubsystem.h"
#include "libJPG/jpge.h"
#if WITH_EDITOR
#include "FoliageEditModule.h"
#endif

namespace FoliageSupport
{
	static void EnableRequiredFoliageProperties(ILevelSnapshotsModule& Module)
	{
		FProperty* InstanceReorderTable = UInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UInstancedStaticMeshComponent, InstanceReorderTable));
		
		FProperty* SortedInstances = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, SortedInstances));
		FProperty* NumBuiltInstances = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, NumBuiltInstances));
		FProperty* NumBuiltRenderInstances = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, NumBuiltRenderInstances));
		FProperty* BuiltInstanceBounds = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, BuiltInstanceBounds));
		FProperty* UnbuiltInstanceBounds = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, UnbuiltInstanceBounds));
		FProperty* UnbuiltInstanceBoundsList = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, UnbuiltInstanceBoundsList));
		FProperty* bEnableDensityScaling = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, bEnableDensityScaling));
		FProperty* OcclusionLayerNumNodes = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, OcclusionLayerNumNodes));
		FProperty* CacheMeshExtendedBounds = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, CacheMeshExtendedBounds));
		FProperty* bDisableCollision = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, bDisableCollision));
		FProperty* InstanceCountToRender = UHierarchicalInstancedStaticMeshComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UHierarchicalInstancedStaticMeshComponent, InstanceCountToRender));
		
		if (ensure(InstanceReorderTable))
		{
			Module.AddExplicitilySupportedProperties({
				InstanceReorderTable,
				SortedInstances,
				NumBuiltInstances,
				NumBuiltRenderInstances,
				BuiltInstanceBounds,
				UnbuiltInstanceBounds,
				UnbuiltInstanceBoundsList,
				bEnableDensityScaling,
				OcclusionLayerNumNodes,
				CacheMeshExtendedBounds,
				bDisableCollision,
				InstanceCountToRender
			});
		}
	}
}

void FFoliageSupport::Register(ILevelSnapshotsModule& Module)
{
	FoliageSupport::EnableRequiredFoliageProperties(Module);
	
	const TSharedRef<FFoliageSupport> FoliageSupport = MakeShared<FFoliageSupport>();
	Module.RegisterRestorabilityOverrider(FoliageSupport);
	Module.RegisterRestorationListener(FoliageSupport);
	Module.RegisterCustomObjectSerializer(AInstancedFoliageActor::StaticClass(), FoliageSupport);
}

ISnapshotRestorabilityOverrider::ERestorabilityOverride FFoliageSupport::IsActorDesirableForCapture(const AActor* Actor)
{
	// Foliage's not allowed by default because it is hidden from the scene outliner
	return Actor->GetClass() == AInstancedFoliageActor::StaticClass() || FFoliageHelper::IsOwnedByFoliage(Actor)
		? ERestorabilityOverride::Allow : ERestorabilityOverride::DoNotCare;
}

void FFoliageSupport::OnTakeSnapshot(UObject* EditorObject, ICustomSnapshotSerializationData& DataStorage)
{
	AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(EditorObject);
	check(FoliageActor);

	FInstancedFoliageActorData FoliageData;
	DataStorage.WriteObjectAnnotation(FObjectAnnotator::CreateLambda([&FoliageData, FoliageActor](FArchive& Archive)
	{
		FoliageData.Save(Archive.GetCustomVersions(), FoliageActor);
		Archive << FoliageData;
	}));
}

void FFoliageSupport::PostApplySnapshotProperties(UObject* Object, const ICustomSnapshotSerializationData& DataStorage)
{
	AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(Object);
	check(FoliageActor);
	
	// Track this actor for safety so we know for sure that the functions were called in the order we expected them to
	CurrentFoliageActor = FoliageActor;
	DataStorage.ReadObjectAnnotation(FObjectAnnotator::CreateLambda([this](FArchive& Archive)
	{
		// "Archive" uses versioning information stored in the snapshot: it can just be reused 
		CurrentVersionInfo = Archive.GetCustomVersions();
		Archive << CurrentFoliageData;
	}));

	// Rest is done in PostApplySnapshotToActor (need access to the property selection map)
}

namespace FoliageSupport
{
	static void RebuildChangedFoliageComponents(AInstancedFoliageActor* FoliageActor, const FPropertySelectionMap& SelectionMap, bool bWasRecreated)
	{
		const FRestorableObjectSelection FoliageSelection = SelectionMap.GetObjectSelection(FoliageActor);
		const FAddedAndRemovedComponentInfo* RecreatedComponentInfo = FoliageSelection.GetComponentSelection();
		
		TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> Components(FoliageActor);
		for (UHierarchicalInstancedStaticMeshComponent* Comp : Components)
		{
			const FRestorableObjectSelection ObjectSelection = SelectionMap.GetObjectSelection(Comp);

			const FPropertySelection* CompPropertySelection = ObjectSelection.GetPropertySelection();
			const bool bHasChangedProperties = CompPropertySelection && !CompPropertySelection->IsEmpty();
			const bool bComponentWasRecreated = RecreatedComponentInfo && Algo::FindByPredicate(RecreatedComponentInfo->SnapshotComponentsToAdd, [Comp](TWeakObjectPtr<UActorComponent> SnapshotComp)
			{
				return SnapshotComp->GetFName().IsEqual(Comp->GetFName());
			}) != nullptr;
			
			if (bWasRecreated || bHasChangedProperties || bComponentWasRecreated)
			{
				// Unregister and register recreates render state: otherwise instances won't show
				Comp->UnregisterComponent();
				
				// This recomputes transforms and shadows
				constexpr bool bAsync = false;
				constexpr bool bForceUpdate = true;
				Comp->BuildTreeIfOutdated(bAsync, bForceUpdate);
				
				Comp->RegisterComponent();
			}
		}
	}
	
	static void UpdateFoliageUI()
	{
#if WITH_EDITOR
		IFoliageEditModule& FoliageEditModule = FModuleManager::Get().GetModuleChecked<IFoliageEditModule>("FoliageEdit");
		FoliageEditModule.UpdateMeshList();
#endif
	}
}

void FFoliageSupport::PostApplySnapshotToActor(const FApplySnapshotToActorParams& Params)
{
	// This is the order of operations up until now
	// 1. OnTakeSnapshot > Set initial data
	// 2. User requests apply to world
	// 3. FindOrRecreateSubobjectInEditorWorld allocates the UFoliageType subobjects
	// 4. PreRemoveComponent
	// 5. PostApplySnapshotProperties loads the data that OnTakeSnapshot saved
	
	if (AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(Params.Actor))
	{
		checkf(FoliageActor == CurrentFoliageActor.Get(), TEXT("PostApplySnapshotToActor was not directly followed by PostApplySnapshotProperties. Investigate."));
		CurrentFoliageActor.Reset();
#if WITH_EDITOR
		// Actor might not have been modified because none of its uproperties were changed
		FoliageActor->Modify();
#endif

		CurrentFoliageData.ApplyTo(CurrentVersionInfo, FoliageActor, Params.SelectedProperties, Params.bWasRecreated);
		FoliageSupport::RebuildChangedFoliageComponents(FoliageActor, Params.SelectedProperties, Params.bWasRecreated);

		FoliageTypesToRemove.Reset();
		FoliageSupport::UpdateFoliageUI();
	}
}

void FFoliageSupport::PreApplySnapshotProperties(const FApplySnapshotPropertiesParams& Params)
{
	// RF_Transactional is temporarily required because Level Snapshots changes components differently than Foliage originally designed.
	// Needed for correct undo / redo 
	if (UHierarchicalInstancedStaticMeshComponent* Comp = Cast<UHierarchicalInstancedStaticMeshComponent>(Params.Object); Comp && Comp->IsIn(CurrentFoliageActor.Get()))
	{
		Comp->SetFlags(RF_Transactional);
	}
}

void FFoliageSupport::PostApplySnapshotProperties(const FApplySnapshotPropertiesParams& Params)
{
	if (UHierarchicalInstancedStaticMeshComponent* Comp = Cast<UHierarchicalInstancedStaticMeshComponent>(Params.Object); Comp && Comp->IsIn(CurrentFoliageActor.Get()))
	{
		Comp->ClearFlags(RF_Transactional);
	}
}

void FFoliageSupport::PreRecreateActor(UWorld* World, TSubclassOf<AActor> ActorClass, FActorSpawnParameters& InOutSpawnParameters)
{
	if (ActorClass->IsChildOf(AInstancedFoliageActor::StaticClass()))
	{
		InOutSpawnParameters.bCreateActorPackage = true;

		// See AInstancedFoliageActor::GetDefault
		ULevel* CurrentLevel = World->GetCurrentLevel();
		if (CurrentLevel && CurrentLevel->GetWorld()->GetSubsystem<UActorPartitionSubsystem>()->IsLevelPartition())
		{
			InOutSpawnParameters.ObjectFlags |= RF_Transient;
		}
	}
}

void FFoliageSupport::PostRecreateActor(AActor* RecreatedActor)
{
	if (AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(RecreatedActor))
	{
		RecreatedActor->GetLevel()->InstancedFoliageActor = FoliageActor;
	}
}

void FFoliageSupport::PreRemoveActor(AActor* ActorToRemove)
{
	if (AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(ActorToRemove))
	{
		FoliageActor->GetLevel()->InstancedFoliageActor.Reset();
	}
}

namespace FoliageSupport
{
	static UFoliageType* FindFoliageInfoFor(UHierarchicalInstancedStaticMeshComponent* Component)
	{
		AInstancedFoliageActor* Foliage = Cast<AInstancedFoliageActor>(Component->GetOwner());
		if (!LIKELY(Foliage))
		{
			return nullptr;
		}

		for (auto FoliageIt = Foliage->GetFoliageInfos().CreateConstIterator(); FoliageIt; ++FoliageIt)
		{
			if (FoliageIt->Value->Implementation->IsOwnedComponent(Component))
			{
				return FoliageIt->Key;
			}
		}

		return nullptr;
	}

	static UFoliageType* FindFoliageInfoFor(UActorComponent* Component)
	{
		if (UHierarchicalInstancedStaticMeshComponent* Comp = Cast<UHierarchicalInstancedStaticMeshComponent>(Component))
		{
			return FindFoliageInfoFor(Comp);
		}
		return nullptr;
	}
}

void FFoliageSupport::PreRemoveComponent(UActorComponent* ComponentToRemove)
{
	if (UFoliageType* FoliagType = FoliageSupport::FindFoliageInfoFor(ComponentToRemove))
	{
		AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(ComponentToRemove->GetOwner());
		FoliageActor->RemoveFoliageType(&FoliagType, 1);
	}
}
