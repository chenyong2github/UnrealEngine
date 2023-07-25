// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGActorAndComponentMapping.h"

#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGModule.h"
#include "PCGSubsystem.h"
#include "PCGWorldActor.h"
#include "Grid/PCGPartitionActor.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGHelpers.h"

#include "LandscapeProxy.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

namespace PCGActorAndComponentMapping
{
	FBox GetActorBounds(const AActor* InActor)
	{
		FBox ActorBounds = InActor->GetComponentsBoundingBox();
		if (!ActorBounds.IsValid && InActor->GetRootComponent() != nullptr)
		{
			// Try on the RootComponent
			ActorBounds = InActor->GetRootComponent()->Bounds.GetBox();
		}

		return ActorBounds;
	}
}

UPCGActorAndComponentMapping::UPCGActorAndComponentMapping(UPCGSubsystem* InPCGSubsystem)
	: PCGSubsystem(InPCGSubsystem)
{
	check(PCGSubsystem);

	// TODO: For now we set our octree to be 2km wide, but it would be perhaps better to
	// scale it to the size of our world.
	constexpr FVector::FReal OctreeExtent = 200000; // 2km
	PartitionedOctree.Reset(FVector::ZeroVector, OctreeExtent);
	NonPartitionedOctree.Reset(FVector::ZeroVector, OctreeExtent);
}

void UPCGActorAndComponentMapping::Tick()
{
	TSet<TObjectPtr<UPCGComponent>> ComponentToUnregister;
	{
		FScopeLock Lock(&DelayedComponentToUnregisterLock);
		ComponentToUnregister = MoveTemp(DelayedComponentToUnregister);
	}

	for (UPCGComponent* Component : ComponentToUnregister)
	{
		UnregisterPCGComponent(Component, /*bForce=*/true);
	}
}

TArray<FPCGTaskId> UPCGActorAndComponentMapping::DispatchToRegisteredLocalComponents(UPCGComponent* OriginalComponent, const TFunction<FPCGTaskId(UPCGComponent*)>& InFunc) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::DispatchToRegisteredLocalComponents);

	// TODO: Might be more interesting to copy the set and release the lock.
	FReadScopeLock ReadLock(ComponentToPartitionActorsMapLock);
	const TSet<TObjectPtr<APCGPartitionActor>>* PartitionActorsPtr = ComponentToPartitionActorsMap.Find(OriginalComponent);

	if (!PartitionActorsPtr)
	{
		return TArray<FPCGTaskId>();
	}

	return DispatchToLocalComponents(OriginalComponent, *PartitionActorsPtr, InFunc);
}

TArray<FPCGTaskId> UPCGActorAndComponentMapping::DispatchToLocalComponents(UPCGComponent* OriginalComponent, const TSet<TObjectPtr<APCGPartitionActor>>& PartitionActors, const TFunction<FPCGTaskId(UPCGComponent*)>& InFunc) const
{
	TArray<FPCGTaskId> TaskIds;
	for (APCGPartitionActor* PartitionActor : PartitionActors)
	{
		if (PartitionActor)
		{
			if (UPCGComponent* LocalComponent = PartitionActor->GetLocalComponent(OriginalComponent))
			{
				// Add check to avoid infinite loop
				if (ensure(!LocalComponent->IsPartitioned()))
				{
					FPCGTaskId LocalTask = InFunc(LocalComponent);

					if (LocalTask != InvalidPCGTaskId)
					{
						TaskIds.Add(LocalTask);
					}
				}
			}
		}
	}

	return TaskIds;
}

bool UPCGActorAndComponentMapping::RegisterOrUpdatePCGComponent(UPCGComponent* InComponent, bool bDoActorMapping)
{
	check(InComponent);

	// Discard BP templates, local components and invalid component
	if (!IsValid(InComponent) || !InComponent->GetOwner() || InComponent->GetOwner()->IsA<APCGPartitionActor>())
	{
		return false;
	}

	// Check also that the bounds are valid. If not early out.
	if (!InComponent->GetGridBounds().IsValid)
	{
		UE_LOG(LogPCG, Error, TEXT("[RegisterOrUpdatePCGComponent] Component has invalid bounds, not registered nor updated."));
		return false;
	}

	const bool bWasAlreadyRegistered = NonPartitionedOctree.Contains(InComponent) || PartitionedOctree.Contains(InComponent);

	// First check if the component has changed its partitioned flag.
	const bool bIsPartitioned = InComponent->IsPartitioned();
	if (bIsPartitioned && NonPartitionedOctree.Contains(InComponent))
	{
		UnregisterNonPartitionedPCGComponent(InComponent);
	}
	else if (!bIsPartitioned && PartitionedOctree.Contains(InComponent))
	{
		UnregisterPartitionedPCGComponent(InComponent);
	}

	// Then register/update accordingly
	bool bHasChanged = false;
	if (bIsPartitioned)
	{
		bHasChanged = RegisterOrUpdatePartitionedPCGComponent(InComponent, bDoActorMapping);
	}
	else
	{
		bHasChanged = RegisterOrUpdateNonPartitionedPCGComponent(InComponent);
	}

	// And finally handle the tracking. Only do it when the component is registered for the first time.
#if WITH_EDITOR
	if (!bWasAlreadyRegistered && bHasChanged)
	{
		RegisterOrUpdateTracking(InComponent, /*bInShouldDirtyActors=*/ false);
	}
#endif // WITH_EDITOR

	return bHasChanged;
}

bool UPCGActorAndComponentMapping::RegisterOrUpdatePartitionedPCGComponent(UPCGComponent* InComponent, bool bDoActorMapping)
{
	FBox Bounds(EForceInit::ForceInit);
	bool bComponentHasChanged = false;
	bool bComponentWasAdded = false;

	PartitionedOctree.AddOrUpdateComponent(InComponent, Bounds, bComponentHasChanged, bComponentWasAdded);

#if WITH_EDITOR
	// In Editor only, we will create new partition actors depending on the new bounds
	// TODO: For now it will always create the PA. But if we want to create them only when we generate, we need to make
	// sure to update the runtime flow, for them to also create PA if they need to.
	if (bComponentHasChanged || bComponentWasAdded)
	{
		bool bHasUnbounded = false;
		PCGHiGenGrid::FSizeArray GridSizes;
		ensure(PCGHelpers::GetGenerationGridSizes(InComponent ? InComponent->GetGraph() : nullptr, PCGSubsystem->GetPCGWorldActor(), GridSizes, bHasUnbounded));
		PCGSubsystem->CreatePartitionActorsWithinBounds(Bounds, GridSizes);
	}
#endif // WITH_EDITOR

	// After adding/updating, try to do the mapping (if we asked for it and the component changed)
	if (bDoActorMapping)
	{
		if (bComponentHasChanged)
		{
			UpdateMappingPCGComponentPartitionActor(InComponent);
		}
	}
	else
	{
		if (!bComponentWasAdded)
		{
			// If we do not want a mapping, delete the existing one
			DeleteMappingPCGComponentPartitionActor(InComponent);
		}
	}

	return bComponentHasChanged;
}

bool UPCGActorAndComponentMapping::RegisterOrUpdateNonPartitionedPCGComponent(UPCGComponent* InComponent)
{
	// Tracking is only done in Editor for now
#if WITH_EDITOR
	FBox Bounds(EForceInit::ForceInit);
	bool bComponentHasChanged = false;
	bool bComponentWasAdded = false;

	NonPartitionedOctree.AddOrUpdateComponent(InComponent, Bounds, bComponentHasChanged, bComponentWasAdded);

	return bComponentHasChanged;
#else
	return false;
#endif // WITH_EDITOR
}

bool UPCGActorAndComponentMapping::RemapPCGComponent(const UPCGComponent* OldComponent, UPCGComponent* NewComponent, bool bDoActorMapping)
{
	check(OldComponent && NewComponent);

	bool bBoundsChanged = false;

	if (OldComponent->IsPartitioned())
	{
		if (!PartitionedOctree.RemapComponent(OldComponent, NewComponent, bBoundsChanged))
		{
			return false;
		}
	}
	else
	{
		if (!NonPartitionedOctree.RemapComponent(OldComponent, NewComponent, bBoundsChanged))
		{
			return false;
		}
	}

	// Remove it from the delayed
	{
		FScopeLock Lock(&DelayedComponentToUnregisterLock);
		DelayedComponentToUnregister.Remove(OldComponent);
	}

	// Remap all previous instances
	{
		FWriteScopeLock WriteLock(ComponentToPartitionActorsMapLock);
		TSet<TObjectPtr<APCGPartitionActor>>* PartitionActorsPtr = ComponentToPartitionActorsMap.Find(OldComponent);

		if (PartitionActorsPtr)
		{
			TSet<TObjectPtr<APCGPartitionActor>> PartitionActorsToRemap = MoveTemp(*PartitionActorsPtr);
			ComponentToPartitionActorsMap.Remove(OldComponent);

			for (APCGPartitionActor* Actor : PartitionActorsToRemap)
			{
				Actor->RemapGraphInstance(OldComponent, NewComponent);
			}

			ComponentToPartitionActorsMap.Add(NewComponent, MoveTemp(PartitionActorsToRemap));
		}
	}

	// And update the mapping if bounds changed and we want to do actor mapping
	if (bBoundsChanged && NewComponent->IsPartitioned() && bDoActorMapping)
	{
		UpdateMappingPCGComponentPartitionActor(NewComponent);
	}

#if WITH_EDITOR
	RemapTracking(OldComponent, NewComponent);
#endif // WITH_EDITOR

	return true;
}

void UPCGActorAndComponentMapping::UnregisterPCGComponent(UPCGComponent* InComponent, bool bForce)
{
	if (!InComponent)
	{
		return;
	}

	if ((PartitionedOctree.Contains(InComponent) || NonPartitionedOctree.Contains(InComponent)))
	{
		// We also need to check that our current PCG Component is not deleted while being reconstructed by a construction script.
		// If so, it will be "re-created" at some point with the same properties.
		// In this particular case, we don't remove the PCG component from the octree and we won't delete the mapping, but mark it to be removed
		// at next Subsystem tick. If we call "RemapPCGComponent" before, we will re-connect everything correctly.
		// Ignore this if we force (aka when we actually unregister the delayed one)
		if (InComponent->IsCreatedByConstructionScript() && !bForce)
		{
			FScopeLock Lock(&DelayedComponentToUnregisterLock);
			DelayedComponentToUnregister.Add(InComponent);
			return;
		}
	}

	UnregisterPartitionedPCGComponent(InComponent);
	UnregisterNonPartitionedPCGComponent(InComponent);

#if WITH_EDITOR
	UnregisterTracking(InComponent);
#endif // WITH_EDITOR
}

void UPCGActorAndComponentMapping::UnregisterPartitionedPCGComponent(UPCGComponent* InComponent)
{
	if (!PartitionedOctree.RemoveComponent(InComponent))
	{
		return;
	}

	// Because of recursive component deletes actors that has components, we cannot do RemoveGraphInstance
	// inside a lock. So copy the actors to clean up and release the lock before doing RemoveGraphInstance.
	TSet<TObjectPtr<APCGPartitionActor>> PartitionActorsToCleanUp;
	{
		FWriteScopeLock WriteLock(ComponentToPartitionActorsMapLock);
		TSet<TObjectPtr<APCGPartitionActor>>* PartitionActorsPtr = ComponentToPartitionActorsMap.Find(InComponent);

		if (PartitionActorsPtr)
		{
			PartitionActorsToCleanUp = MoveTemp(*PartitionActorsPtr);
			ComponentToPartitionActorsMap.Remove(InComponent);
		}
	}

	for (APCGPartitionActor* Actor : PartitionActorsToCleanUp)
	{
		Actor->RemoveGraphInstance(InComponent);
	}
}

void UPCGActorAndComponentMapping::UnregisterNonPartitionedPCGComponent(UPCGComponent* InComponent)
{
	NonPartitionedOctree.RemoveComponent(InComponent);
}

void UPCGActorAndComponentMapping::ForAllIntersectingComponents(const FBoxCenterAndExtent& InBounds, TFunction<void(UPCGComponent*)> InFunc) const
{
	PartitionedOctree.FindElementsWithBoundsTest(InBounds, [&InFunc](const FPCGComponentRef& ComponentRef)
	{
		InFunc(ComponentRef.Component);
	});
}

void UPCGActorAndComponentMapping::RegisterPartitionActor(APCGPartitionActor* Actor, bool bDoComponentMapping)
{
	check(Actor);

	FIntVector GridCoord = Actor->GetGridCoord();
	{
		FWriteScopeLock WriteLock(PartitionActorsMapLock);

		TMap<FIntVector, TObjectPtr<APCGPartitionActor>>& PartitionActorsMapGrid = PartitionActorsMap.FindOrAdd(Actor->GetPCGGridSize());
		if (PartitionActorsMapGrid.Contains(GridCoord))
		{
			return;
		}

		PartitionActorsMapGrid.Add(GridCoord, Actor);
	}

	// For deprecration: bUse2DGrid is now true by default. But if we already have Partition Actors that were created when the flag was false by default,
	// we keep this flag
	if (APCGWorldActor* WorldActor = PCGSubsystem->GetPCGWorldActor())
	{
		if (WorldActor->bUse2DGrid != Actor->IsUsing2DGrid())
		{
			WorldActor->bUse2DGrid = Actor->IsUsing2DGrid();
		}
	}

	// And then register itself to all the components that intersect with it
	{
		FWriteScopeLock WriteLock(ComponentToPartitionActorsMapLock);
		ForAllIntersectingComponents(FBoxCenterAndExtent(Actor->GetFixedBounds()), [this, Actor, bDoComponentMapping](UPCGComponent* Component)
		{
			// For each component, do the mapping if we ask it explicitly, or if the component is generated
			if (bDoComponentMapping || Component->bGenerated)
			{
				TSet<TObjectPtr<APCGPartitionActor>>* PartitionActorsPtr = ComponentToPartitionActorsMap.Find(Component);
				// In editor we might load/create partition actors while the component is registering. Because of that,
				// the mapping might not already exists, even if the component is marked generated.
				if (PartitionActorsPtr)
				{
					Actor->AddGraphInstance(Component);
					PartitionActorsPtr->Add(Actor);
				}
			}
		});
	}
}

void UPCGActorAndComponentMapping::UnregisterPartitionActor(APCGPartitionActor* Actor)
{
	check(Actor);

	FIntVector GridCoord = Actor->GetGridCoord();

	if (TMap<FIntVector, TObjectPtr<APCGPartitionActor>>* PartitionActorsMapGrid = PartitionActorsMap.Find(Actor->GetPCGGridSize()))
	{
		FWriteScopeLock WriteLock(PartitionActorsMapLock);
		PartitionActorsMapGrid->Remove(GridCoord);
	}

	// And then unregister itself to all the components that intersect with it
	{
		FWriteScopeLock WriteLock(ComponentToPartitionActorsMapLock);
		ForAllIntersectingComponents(FBoxCenterAndExtent(Actor->GetFixedBounds()), [this, Actor](UPCGComponent* Component)
		{
			TSet<TObjectPtr<APCGPartitionActor>>* PartitionActorsPtr = ComponentToPartitionActorsMap.Find(Component);
		if (PartitionActorsPtr)
		{
			PartitionActorsPtr->Remove(Actor);
		}
		});
	}
}

void UPCGActorAndComponentMapping::ForAllIntersectingPartitionActors(const FBox& InBounds, TFunction<void(APCGPartitionActor*)> InFunc) const
{
	// No PCGWorldActor just early out. Same for invalid bounds.
	APCGWorldActor* PCGWorldActor = PCGSubsystem->GetPCGWorldActor();

	if (!PCGWorldActor || !InBounds.IsValid)
	{
		return;
	}

	PCGHiGenGrid::FSizeToGuidMap GridSizeToGuid;
	PCGWorldActor->GetGridGuids(GridSizeToGuid);
	for (const TPair<uint32, FGuid>& SizeAndGuid : GridSizeToGuid)
	{
		const uint32 GridSize = SizeAndGuid.Key;

		const bool bUse2DGrid = PCGWorldActor->bUse2DGrid;
		FIntVector MinCellCoords = UPCGActorHelpers::GetCellCoord(InBounds.Min, GridSize, bUse2DGrid);
		FIntVector MaxCellCoords = UPCGActorHelpers::GetCellCoord(InBounds.Max, GridSize, bUse2DGrid);

		FReadScopeLock ReadLock(PartitionActorsMapLock);

		const TMap<FIntVector, TObjectPtr<APCGPartitionActor>>* PartitionActorsMapGrid = PartitionActorsMap.Find(GridSize);
		if (!PartitionActorsMapGrid || PartitionActorsMapGrid->IsEmpty())
		{
			continue;
		}

		for (int32 z = MinCellCoords.Z; z <= MaxCellCoords.Z; z++)
		{
			for (int32 y = MinCellCoords.Y; y <= MaxCellCoords.Y; y++)
			{
				for (int32 x = MinCellCoords.X; x <= MaxCellCoords.X; x++)
				{
					FIntVector CellCoords(x, y, z);
					if (const TObjectPtr<APCGPartitionActor>* ActorPtr = PartitionActorsMapGrid->Find(CellCoords))
					{
						if (APCGPartitionActor* Actor = ActorPtr->Get())
						{
							InFunc(Actor);
						}
					}
				}
			}
		}
	}
}

void UPCGActorAndComponentMapping::UpdateMappingPCGComponentPartitionActor(UPCGComponent* InComponent)
{
	if (!PCGSubsystem->IsInitialized())
	{
		return;
	}

	check(InComponent);

	// Get the bounds
	FBox Bounds = PartitionedOctree.GetBounds(InComponent);

	if (!Bounds.IsValid)
	{
		return;
	}

	TSet<TObjectPtr<APCGPartitionActor>> RemovedActors;

	{
		FWriteScopeLock WriteLock(ComponentToPartitionActorsMapLock);

		TSet<TObjectPtr<APCGPartitionActor>>* PartitionActorsPtr = ComponentToPartitionActorsMap.Find(InComponent);

		if (!PartitionActorsPtr)
		{
			// Does not yet exists, add it
			PartitionActorsPtr = &ComponentToPartitionActorsMap.Emplace(InComponent);
			check(PartitionActorsPtr);
		}

		if (const APCGWorldActor* WorldActor = PCGSubsystem->GetPCGWorldActor())
		{
			const bool bIsHiGenEnabled = InComponent->GetGraph() && InComponent->GetGraph()->IsHierarchicalGenerationEnabled();

			TSet<TObjectPtr<APCGPartitionActor>> NewMapping;
			ForAllIntersectingPartitionActors(Bounds, [&NewMapping, InComponent, WorldActor, bIsHiGenEnabled](APCGPartitionActor* Actor)
			{
				// If this graph does not have HiGen enabled, we should only add a graph instance for
				// the partition actors whose grid size matches the WorldActor's partition grid size
				if (bIsHiGenEnabled || (Actor && Actor->GetPCGGridSize() == WorldActor->PartitionGridSize))
				{
					Actor->AddGraphInstance(InComponent);
					NewMapping.Add(Actor);
				}
			});

			// Find the ones that were removed
			RemovedActors = PartitionActorsPtr->Difference(NewMapping);

			*PartitionActorsPtr = MoveTemp(NewMapping);
		}
	}

	// No need to be locked to do this.
	for (APCGPartitionActor* RemovedActor : RemovedActors)
	{
		RemovedActor->RemoveGraphInstance(InComponent);
	}
}

void UPCGActorAndComponentMapping::DeleteMappingPCGComponentPartitionActor(UPCGComponent* InComponent)
{
	check(InComponent);

	if (!InComponent->IsPartitioned())
	{
		return;
	}

	FWriteScopeLock WriteLock(ComponentToPartitionActorsMapLock);

	TSet<TObjectPtr<APCGPartitionActor>>* PartitionActorsPtr = ComponentToPartitionActorsMap.Find(InComponent);
	if (PartitionActorsPtr)
	{
		for (APCGPartitionActor* Actor : *PartitionActorsPtr)
		{
			Actor->RemoveGraphInstance(InComponent);
		}

		PartitionActorsPtr->Empty();
	}
}

TSet<TObjectPtr<UPCGComponent>> UPCGActorAndComponentMapping::GetAllRegisteredPartitionedComponents() const
{
	return PartitionedOctree.GetAllComponents();
}

TSet<TObjectPtr<UPCGComponent>> UPCGActorAndComponentMapping::GetAllRegisteredNonPartitionedComponents() const
{
	return NonPartitionedOctree.GetAllComponents();
}

TSet<TObjectPtr<UPCGComponent>> UPCGActorAndComponentMapping::GetAllRegisteredComponents() const
{
	TSet<TObjectPtr<UPCGComponent>> Res = GetAllRegisteredPartitionedComponents();
	Res.Append(GetAllRegisteredNonPartitionedComponents());
	return Res;
}

#if WITH_EDITOR
void UPCGActorAndComponentMapping::RegisterOrUpdateTracking(UPCGComponent* InComponent, bool bInShouldDirtyActors)
{
	// Discard BP templates, local components and invalid component
	if (!IsValid(InComponent) || !InComponent->GetOwner() || InComponent->GetOwner()->IsA<APCGPartitionActor>())
	{
		return;
	}

	AActor* ComponentOwner = InComponent->GetOwner();

	// If we have no owner, we might be in a BP so don't track
	if (!ComponentOwner)
	{
		return;
	}

	// Components owner needs to be always tracked
	RegisterActor(ComponentOwner);
	TSet<TObjectPtr<UPCGComponent>>& AllComponents = AlwaysTrackedActorsToComponentsMap.FindOrAdd(ComponentOwner);
	AllComponents.Add(InComponent);

	UWorld* World = PCGSubsystem->GetWorld();
	APCGWorldActor* PCGWorldActor = PCGSubsystem ? PCGSubsystem->GetPCGWorldActor() : nullptr;

	if (!World || !PCGWorldActor)
	{
		return;
	}

	// And we also need to find all actors that should be tracked
	if (UPCGGraph* PCGGraph = InComponent->GetGraph())
	{
		auto FindActorsAndTrack = [this, InComponent, PCGWorldActor, bInShouldDirtyActors](const FPCGActorSelectionKey& InKey, const TArray<FPCGSettingsAndCulling>& InSettingsAndCulling)
		{
			// InKey provide the info for selecting a given actor.
			// We reconstruct the selector settings from this key, and we also force it to SelectMultiple, since
			// we want to gather all the actors that matches this given key.
			FPCGActorSelectorSettings SelectorSettings = FPCGActorSelectorSettings::ReconstructFromKey(InKey);
			SelectorSettings.bSelectMultiple = true;

			TArray<AActor*> AllActors = PCGActorSelector::FindActors(SelectorSettings, InComponent, [](const AActor*) { return true; }, [](const AActor*) { return true; });

			bool bShouldCull = true;
			for (const FPCGSettingsAndCulling& SettingsAndCulling : InSettingsAndCulling)
			{
				if (!SettingsAndCulling.Value)
				{
					bShouldCull = false;
					break;
				}
			}

			for (AActor* Actor : AllActors)
			{
				if (bShouldCull)
				{
					CulledTrackedActorsToComponentsMap.FindOrAdd(Actor).Add(InComponent);
				}
				else
				{
					AlwaysTrackedActorsToComponentsMap.FindOrAdd(Actor).Add(InComponent);
				}

				RegisterActor(Actor);

				if (bInShouldDirtyActors)
				{
					// If we need to force dirty, disregard culling (always intersect).
					InComponent->DirtyTrackedActor(Actor, /*bIntersect=*/ true, {});
				}
			}
		};

		for (TPair<FPCGActorSelectionKey, TArray<FPCGSettingsAndCulling>>& It : PCGGraph->GetTrackedActorKeysToSettings())
		{
			if (!KeysToComponentsMap.Contains(It.Key))
			{
				FindActorsAndTrack(It.Key, It.Value);
			}

			KeysToComponentsMap.FindOrAdd(It.Key).Add(InComponent);
		}

		// Also while we support landscape pins on input node, we need to track landscape if we uses it, or the input is landscape.
		if (InComponent->ShouldTrackLandscape())
		{
			// Landscape doesn't have an associated setting and is always culled.
			FPCGActorSelectionKey LandscapeKey = FPCGActorSelectionKey(ALandscapeProxy::StaticClass());
			if (!KeysToComponentsMap.Contains(LandscapeKey))
			{
				FindActorsAndTrack(LandscapeKey, { {nullptr, true} });
			}

			KeysToComponentsMap.FindOrAdd(LandscapeKey).Add(InComponent);
		}
	}
}

void UPCGActorAndComponentMapping::RemapTracking(const UPCGComponent* InOldComponent, UPCGComponent* InNewComponent)
{
	auto ReplaceInMap = [InOldComponent, InNewComponent](auto& InMap)
	{
		for (auto& It : InMap)
		{
			if (It.Value.Remove(InOldComponent) > 0)
			{
				It.Value.Add(InNewComponent);
			}
		}
	};

	ReplaceInMap(CulledTrackedActorsToComponentsMap);
	ReplaceInMap(AlwaysTrackedActorsToComponentsMap);
}

void UPCGActorAndComponentMapping::UnregisterTracking(UPCGComponent* InComponent)
{
	TSet<TWeakObjectPtr<AActor>> CandidatesForUntrack;
	TSet<FPCGActorSelectionKey> KeysToRemove;

	auto RemoveFromMap = [InComponent](auto& InMap, auto& InCandidateToRemove)
	{
		for (auto& It : InMap)
		{
			It.Value.Remove(InComponent);
			if (It.Value.IsEmpty())
			{
				InCandidateToRemove.Add(It.Key);
			}
		}
	};

	RemoveFromMap(CulledTrackedActorsToComponentsMap, CandidatesForUntrack);
	RemoveFromMap(AlwaysTrackedActorsToComponentsMap, CandidatesForUntrack);
	RemoveFromMap(KeysToComponentsMap, KeysToRemove);

	for (const FPCGActorSelectionKey& Key : KeysToRemove)
	{
		KeysToComponentsMap.Remove(Key);
	}

	// We also need to untrack actors that doesn't have any component that tracks them.
	auto ShouldBeRemoved = [](const TWeakObjectPtr<AActor>& InActor, TMap<TWeakObjectPtr<AActor>, TSet<TObjectPtr<UPCGComponent>>>& InMap)
	{
		TSet<TObjectPtr<UPCGComponent>>* RegisteredComponents = InMap.Find(InActor);
		return !RegisteredComponents || RegisteredComponents->IsEmpty();
	};

	for (TWeakObjectPtr<AActor> Candidate : CandidatesForUntrack)
	{
		if (ShouldBeRemoved(Candidate, CulledTrackedActorsToComponentsMap) && ShouldBeRemoved(Candidate, AlwaysTrackedActorsToComponentsMap))
		{
			UnregisterActor(Candidate.Get());
		}
	}
}

void UPCGActorAndComponentMapping::ResetPartitionActorsMap()
{
	PartitionActorsMapLock.WriteLock();
	PartitionActorsMap.Empty();
	PartitionActorsMapLock.WriteUnlock();
}

void UPCGActorAndComponentMapping::RegisterTrackingCallbacks()
{
	GEngine->OnActorMoved().AddRaw(this, &UPCGActorAndComponentMapping::OnActorMoved);
	GEngine->OnLevelActorAdded().AddRaw(this, &UPCGActorAndComponentMapping::OnActorAdded);
	GEngine->OnLevelActorDeleted().AddRaw(this, &UPCGActorAndComponentMapping::OnActorDeleted);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &UPCGActorAndComponentMapping::OnObjectPropertyChanged);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddRaw(this, &UPCGActorAndComponentMapping::OnPreObjectPropertyChanged);
}

void UPCGActorAndComponentMapping::TeardownTrackingCallbacks()
{
	GEngine->OnActorMoved().RemoveAll(this);
	GEngine->OnLevelActorAdded().RemoveAll(this);
	GEngine->OnLevelActorDeleted().RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);
}

void UPCGActorAndComponentMapping::AddActorsPostInit()
{
	// Safeguard, we can't add delayed actors if the subsystem is not initialized
	if (!PCGSubsystem || !PCGSubsystem->IsInitialized())
	{
		return;
	}

	for (TWeakObjectPtr<AActor>& ActorPtr : DelayedAddedActors)
	{
		OnActorAdded(ActorPtr.Get());
	}

	DelayedAddedActors.Empty();

	// Also add the one tracked by the World Actor
	if (APCGWorldActor* PCGWorldActor = PCGSubsystem->FindPCGWorldActor())
	{
		// Making a copy, since CachedTrackedActors can be modified (if an actor is no longer tracked)
		TSet<TWeakObjectPtr<AActor>> CachedTrackedActorsCopy = PCGWorldActor->CachedTrackedActors;
		for (TWeakObjectPtr<AActor>& ActorPtr : CachedTrackedActorsCopy)
		{
			AddOrUpdateTrackedActor(ActorPtr.Get());
		}
	}
}

void UPCGActorAndComponentMapping::OnActorAdded(AActor* InActor)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnActorAdded);

	// We have to make sure to not create a infinite loop
	if (!InActor || InActor->IsA<APCGWorldActor>() || !PCGSubsystem)
	{
		return;
	}

	// If the subsystem is not initialized, wait for it to be, and store all the actors to check
	if (!PCGSubsystem->IsInitialized())
	{
		DelayedAddedActors.Add(InActor);
		return;
	}

	if (AddOrUpdateTrackedActor(InActor))
	{
		// Finally notify them all
		OnActorChanged(InActor, /*bInHasMoved=*/ false);
	}
}

bool UPCGActorAndComponentMapping::AddOrUpdateTrackedActor(AActor* InActor)
{
	// We have to make sure to not create a infinite loop
	if (!InActor || InActor->IsA<APCGWorldActor>() || !PCGSubsystem || !PCGSubsystem->FindPCGWorldActor())
	{
		return false;
	}

	// Gather all components, and check if they want to track this one
	TSet<TObjectPtr<UPCGComponent>> AllComponents = GetAllRegisteredComponents();

	TSet<TObjectPtr<UPCGComponent>>* CulledTrackedComponents = nullptr;
	TSet<TObjectPtr<UPCGComponent>>* AlwaysTrackedComponents = nullptr;
	
	for (UPCGComponent* PCGComponent : AllComponents)
	{
		bool bTrackingIsCulled = false;
		if (PCGComponent && PCGComponent->IsActorTracked(InActor, bTrackingIsCulled))
		{
			if (bTrackingIsCulled)
			{
				if (!CulledTrackedComponents)
				{
					CulledTrackedComponents = &CulledTrackedActorsToComponentsMap.FindOrAdd(InActor);
				}

				check(CulledTrackedComponents);
				CulledTrackedComponents->Add(PCGComponent);
			}
			else
			{
				if (!AlwaysTrackedComponents)
				{
					AlwaysTrackedComponents = &AlwaysTrackedActorsToComponentsMap.FindOrAdd(InActor);
				}

				check(AlwaysTrackedComponents);
				AlwaysTrackedComponents->Add(PCGComponent);
			}
		}
	}

	if (CulledTrackedComponents || AlwaysTrackedComponents)
	{
		RegisterActor(InActor);
		return true;
	}
	else
	{
		// Do some cleanup. We will force the refresh here, so return false to make sure we don't refresh it twice.
		OnActorDeleted(InActor);
		return false;
	}
}

void UPCGActorAndComponentMapping::RegisterActor(AActor* InActor)
{
	APCGWorldActor* PCGWorldActor = PCGSubsystem ? PCGSubsystem->FindPCGWorldActor() : nullptr;
	if (!PCGWorldActor || !InActor)
	{
		return;
	}

	if (!PCGWorldActor->CachedTrackedActors.Contains(InActor))
	{
		PCGWorldActor->Modify();
		PCGWorldActor->CachedTrackedActors.Add(InActor);
	}

	if (ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(InActor))
	{
		// Only add it once.
		if (!TrackedActorToPositionMap.Contains(InActor))
		{
			LandscapeProxy->OnComponentDataChanged.AddRaw(this, &UPCGActorAndComponentMapping::OnLandscapeChanged);
		}
	}

	TrackedActorToPositionMap.FindOrAdd(InActor) = PCGActorAndComponentMapping::GetActorBounds(InActor);

	// Also gather dependencies
	PCGHelpers::GatherDependencies(InActor, TrackedActorsToDependenciesMap.FindOrAdd(InActor), 1);
}

bool UPCGActorAndComponentMapping::UnregisterActor(AActor* InActor)
{
	APCGWorldActor* PCGWorldActor = PCGSubsystem ? PCGSubsystem->FindPCGWorldActor() : nullptr;
	if (!PCGWorldActor || !InActor)
	{
		return false;
	}

	if (PCGWorldActor->CachedTrackedActors.Contains(InActor))
	{
		PCGWorldActor->Modify();
		PCGWorldActor->CachedTrackedActors.Remove(InActor);
		TrackedActorToPositionMap.Remove(InActor);
		CulledTrackedActorsToComponentsMap.Remove(InActor);
		AlwaysTrackedActorsToComponentsMap.Remove(InActor);
		TrackedActorsToDependenciesMap.Remove(InActor);

		if (ALandscapeProxy* LandscapeProxy = Cast<ALandscapeProxy>(InActor))
		{
			LandscapeProxy->OnComponentDataChanged.RemoveAll(this);
		}

		return true;
	}
	else
	{
		return false;
	}
}

void UPCGActorAndComponentMapping::OnActorDeleted(AActor* InActor)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnActorDeleted);

	APCGWorldActor* PCGWorldActor = PCGSubsystem ? PCGSubsystem->FindPCGWorldActor() : nullptr;
	if (!PCGWorldActor || !InActor || !PCGWorldActor->CachedTrackedActors.Contains(InActor))
	{
		return;
	}

	// Notify all components that the actor has changed (was removed), but the Refresh will only happen AFTER the actor was actually removed from the world (because of delayed refresh).
	OnActorChanged(InActor, /*bInHasMoved=*/ false);

	// And then delete everything
	UnregisterActor(InActor);
}

void UPCGActorAndComponentMapping::OnActorMoved(AActor* InActor)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnActorMoved);

	APCGWorldActor* PCGWorldActor = PCGSubsystem ? PCGSubsystem->FindPCGWorldActor() : nullptr;
	if (!PCGWorldActor || !InActor || !PCGWorldActor->CachedTrackedActors.Contains(InActor))
	{
		return;
	}

	// Notify all components
	OnActorChanged(InActor, /*bInHasMoved=*/ true);

	// Update Actor position
	if (FBox* ActorBounds = TrackedActorToPositionMap.Find(InActor))
	{
		*ActorBounds = PCGActorAndComponentMapping::GetActorBounds(InActor);
	}
}

void UPCGActorAndComponentMapping::OnPreObjectPropertyChanged(UObject* InObject, const FEditPropertyChain& InEditPropertyChain)
{
	// We want to track tags, to see if a tag was removed
	TempTrackedActorTags.Empty();
	FProperty* MemberProperty = InEditPropertyChain.GetActiveMemberNode() ? InEditPropertyChain.GetActiveMemberNode()->GetValue() : nullptr;
	AActor* Actor = Cast<AActor>(InObject);

	if (!Actor || !MemberProperty || MemberProperty->GetFName() != GET_MEMBER_NAME_CHECKED(AActor, Tags))
	{
		return;
	}

	TempTrackedActorTags = TSet<FName>(Actor->Tags);
}

void UPCGActorAndComponentMapping::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnObjectPropertyChanged);

	bool bValueNotInteractive = (InEvent.ChangeType != EPropertyChangeType::Interactive);
	// Special exception for actor tags, as we can't track otherwise an actor "losing" a tag
	bool bActorTagChange = (InEvent.Property && InEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AActor, Tags));
	if (!bValueNotInteractive && !bActorTagChange)
	{
		return;
	}

	AActor* Actor = Cast<AActor>(InObject);
	APCGWorldActor* PCGWorldActor = PCGSubsystem ? PCGSubsystem->FindPCGWorldActor() : nullptr;

	if (!PCGWorldActor)
	{
		return;
	}

	// If we don't find any actor, try to see if it is a dependency
	if (!Actor)
	{
		for (const TPair<TWeakObjectPtr<AActor>, TSet<TObjectPtr<UObject>>>& TrackedActor : TrackedActorsToDependenciesMap)
		{
			if (TrackedActor.Value.Contains(InObject))
			{
				OnActorChanged(TrackedActor.Key.Get(), /*bInHasMoved=*/ false);
			}
		}

		return;
	}

	// Check if we are not tracking it or is a tag change.
	bool bShouldChange = true;
	if (!PCGWorldActor->CachedTrackedActors.Contains(Actor) || bActorTagChange)
	{
		bShouldChange = AddOrUpdateTrackedActor(Actor);
	}

	if (bShouldChange)
	{
		OnActorChanged(Actor, /*bInHasMoved=*/ false);
	}
}

void UPCGActorAndComponentMapping::OnActorChanged(AActor* InActor, bool bInHasMoved)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnActorChanged);

	check(InActor);
	TSet<UPCGComponent*> DirtyComponents;

	EPCGComponentDirtyFlag DirtyFlag = EPCGComponentDirtyFlag::Actor;
	if (InActor->IsA<ALandscapeProxy>())
	{
		DirtyFlag = DirtyFlag | EPCGComponentDirtyFlag::Landscape;
	}

	// Check if we have a change of tag too
	TSet<FName> RemovedTags = TempTrackedActorTags.Difference(TSet<FName>(InActor->Tags));

	if (TSet<TObjectPtr<UPCGComponent>>* CulledTrackedComponents = CulledTrackedActorsToComponentsMap.Find(InActor))
	{
		// Not const, since it will be updated with old actor bounds
		FBox ActorBounds = PCGActorAndComponentMapping::GetActorBounds(InActor);

		// Then do an octree find to get all components that intersect with this actor.
		// If the actor has moved, we also need to find components that intersected with it before
		// We first do it for non-partitioned, then we do it for partitioned
		auto UpdateNonPartitioned = [&DirtyComponents, InActor, CulledTrackedComponents, &RemovedTags, DirtyFlag](const FPCGComponentRef& ComponentRef) -> void
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnActorChanged::UpdateNonPartitioned);

			if (DirtyComponents.Contains(ComponentRef.Component) || !CulledTrackedComponents->Contains(ComponentRef.Component))
			{
				return;
			}

			if (ComponentRef.Component->DirtyTrackedActor(InActor, /*bIntersect=*/true, RemovedTags))
			{
				ComponentRef.Component->DirtyGenerated(DirtyFlag);
				DirtyComponents.Add(ComponentRef.Component);
			}
		};

		NonPartitionedOctree.FindElementsWithBoundsTest(ActorBounds, UpdateNonPartitioned);

		// For partitioned, we first need to find all components that intersect with our actor and then forward the dirty call to all local components that intersect.
		auto UpdatePartitioned = [this, &DirtyComponents, InActor, CulledTrackedComponents, &ActorBounds, &RemovedTags, DirtyFlag](const FPCGComponentRef& ComponentRef)  -> void
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnActorChanged::UpdatePartitioned);

			if (!CulledTrackedComponents->Contains(ComponentRef.Component))
			{
				return;
			}

			const FBox Overlap = ActorBounds.Overlap(ComponentRef.Bounds.GetBox());
			bool bWasDirtied = false;

			ForAllIntersectingPartitionActors(Overlap, [InActor, Component = ComponentRef.Component, &RemovedTags, &bWasDirtied, DirtyFlag](APCGPartitionActor* InPartitionActor) -> void
			{
				if (UPCGComponent* LocalComponent = InPartitionActor->GetLocalComponent(Component))
				{
					if (LocalComponent->DirtyTrackedActor(InActor, /*bIntersect=*/true, RemovedTags))
					{
						bWasDirtied = true;
						LocalComponent->DirtyGenerated(DirtyFlag);
					}
				}
			});

			if (bWasDirtied)
			{
				// Don't dispatch
				ComponentRef.Component->DirtyGenerated(DirtyFlag, /*bDispatchToLocalComponents=*/false);
				DirtyComponents.Add(ComponentRef.Component);
			}
		};

		PartitionedOctree.FindElementsWithBoundsTest(ActorBounds, UpdatePartitioned);

		// If it has moved, redo it with the old bounds.
		if (bInHasMoved)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnActorChanged::SecondUpdateHasMoved);

			if (FBox* OldActorBounds = TrackedActorToPositionMap.Find(InActor))
			{
				if (!OldActorBounds->Equals(ActorBounds))
				{
					// Set the actor bounds with the old one, to have the right Overlap in the Partition case.
					ActorBounds = *OldActorBounds;
					NonPartitionedOctree.FindElementsWithBoundsTest(*OldActorBounds, UpdateNonPartitioned);
					PartitionedOctree.FindElementsWithBoundsTest(*OldActorBounds, UpdatePartitioned);
				}
			}
		}
	}

	// Finally, dirty all components that always track this actor that are not yet notified.
	if (TSet<TObjectPtr<UPCGComponent>>* AlwaysTrackedComponents = AlwaysTrackedActorsToComponentsMap.Find(InActor))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGActorAndComponentMapping::OnActorChanged::AlwaysTrackedUpdate);

		for (UPCGComponent* PCGComponent : *AlwaysTrackedComponents)
		{
			if (!PCGComponent)
			{
				continue;
			}

			const bool bOwnerChanged = PCGComponent->GetOwner() == InActor;
			bool bWasDirtied = false;

			if (!DirtyComponents.Contains(PCGComponent) && !bOwnerChanged)
			{
				if (PCGComponent->IsPartitioned())
				{
					DispatchToRegisteredLocalComponents(PCGComponent, [InActor, &RemovedTags, &bWasDirtied, DirtyFlag](UPCGComponent* InLocalComponent) -> FPCGTaskId
					{
						if (InLocalComponent->DirtyTrackedActor(InActor, /*bIntersect=*/false, RemovedTags))
						{
							bWasDirtied = true;
							InLocalComponent->DirtyGenerated(DirtyFlag);
						}
						return InvalidPCGTaskId;
					});
				}
				else
				{
					bWasDirtied = PCGComponent->DirtyTrackedActor(InActor, /*bIntersect=*/false, RemovedTags);
				}
			}

			if (bWasDirtied || bOwnerChanged)
			{
				PCGComponent->DirtyGenerated(DirtyFlag, /*bDispatchToLocalComponents=*/bOwnerChanged);
				DirtyComponents.Add(PCGComponent);
			}
		}
	}

	// And refresh all dirtied components
	for (UPCGComponent* Component : DirtyComponents)
	{
		if (Component)
		{
			Component->Refresh();
		}
	}
}

void UPCGActorAndComponentMapping::OnLandscapeChanged(ALandscapeProxy* InLandscape, const FLandscapeProxyComponentDataChangedParams& InChangeParams)
{
	// We don't know if the landscape moved, only that it has changed. Since `bHasMoved` is doing a bit more, always assume that the landscape has moved.
	OnActorChanged(InLandscape, /*bHasMoved=*/true);
}

#endif // WITH_EDITOR