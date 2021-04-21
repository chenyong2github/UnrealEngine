// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterListData.h"

#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsStats.h"

#include "GameFramework/Actor.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void FFilterListData::UpdateFilteredList(UWorld* World, ULevelSnapshot* FromSnapshot, ULevelSnapshotFilter* FilterToApply)
{
	// We only track progress of HandleActorExistsInWorldAndSnapshot because the other two functions are relatively fast in comparison: deserialisation takes much longer.
	const int32 ExpectedAmountOfWork = FromSnapshot->GetNumSavedActors();
	FScopedSlowTask DiffDeserializedActors(ExpectedAmountOfWork, LOCTEXT("DiffingActorsKey", "Diffing actors"));
	DiffDeserializedActors.MakeDialogDelayed(1.f);

    RelatedSnapshot = FromSnapshot;

	// We expect the number of filtered actors & components to stay roughly the same: retain existing memory
	ModifiedActorsSelectedProperties.Empty(false);
	ModifiedFilteredActors.Empty(ModifiedFilteredActors.Num());
	FilteredRemovedOriginalActorPaths.Empty(FilteredRemovedOriginalActorPaths.Num());
	FilteredAddedWorldActors.Empty(FilteredAddedWorldActors.Num());
	
	FromSnapshot->DiffWorld(
		World,
		ULevelSnapshot::FActorPathConsumer::CreateRaw(this, &FFilterListData::HandleActorExistsInWorldAndSnapshot, FilterToApply, &DiffDeserializedActors),
		ULevelSnapshot::FActorPathConsumer::CreateRaw(this, &FFilterListData::HandleActorWasRemovedFromWorld, FilterToApply),
		ULevelSnapshot::FActorConsumer::CreateRaw(this, &FFilterListData::HandleActorWasAddedToWorld, FilterToApply)
		);
}

const FPropertySelectionMap& FFilterListData::ApplyFilterToFindSelectedProperties(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply)
{
	if (!ensure(WorldActor && FilterToApply) || ModifiedActorsSelectedProperties.GetSelectedProperties(WorldActor))
	{
		return ModifiedActorsSelectedProperties;
	}
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ApplyFilterToFindSelectedProperties"), STAT_ApplyFilterToFindSelectedProperties, STATGROUP_LevelSnapshots);

	const TWeakObjectPtr<AActor> DeserializedActor = GetSnapshotCounterpartFor(WorldActor);
	if (DeserializedActor.IsValid())
	{
		ULevelSnapshotsFunctionLibrary::ApplyFilterToFindSelectedProperties(RelatedSnapshot, ModifiedActorsSelectedProperties, WorldActor, DeserializedActor.Get(), FilterToApply);
	}
	return ModifiedActorsSelectedProperties;
}

TWeakObjectPtr<AActor> FFilterListData::GetSnapshotCounterpartFor(TWeakObjectPtr<AActor> WorldActor) const
{
	const TOptional<AActor*> DeserializedActor = RelatedSnapshot->GetDeserializedActor(WorldActor.Get());
	return ensureAlwaysMsgf(DeserializedActor, TEXT("Deserialized actor does no exist. Either the snapshots's container world was deleted or the snapshot has no counterpart for this actor"))
		? *DeserializedActor : nullptr;
}

const FPropertySelectionMap& FFilterListData::GetModifiedActorsSelectedProperties() const
{
	return ModifiedActorsSelectedProperties;
}

const TSet<TWeakObjectPtr<AActor>>& FFilterListData::GetModifiedFilteredActors() const
{
	return ModifiedFilteredActors;
}

const TSet<FSoftObjectPath>& FFilterListData::GetFilteredRemovedOriginalActorPaths() const
{
	return FilteredRemovedOriginalActorPaths;
}

const TSet<TWeakObjectPtr<AActor>>& FFilterListData::GetFilteredAddedWorldActors() const
{
	return FilteredAddedWorldActors;
}

void FFilterListData::HandleActorExistsInWorldAndSnapshot(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply, FScopedSlowTask* Progress)
{
	Progress->EnterProgressFrame();
	
	UObject* ResolvedWorldActor = OriginalActorPath.ResolveObject();
	if (!ResolvedWorldActor)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to resolve actor %s. Was it deleted from the world?"), *OriginalActorPath.ToString());
		return;
	}

	AActor* WorldActor = Cast<AActor>(ResolvedWorldActor);
	if (ensureAlwaysMsgf(WorldActor, TEXT("A path that was previously associated with an actor no longer refers to an actor. Something is wrong.")))
	{
		TOptional<AActor*> DeserializedSnapshotActor = RelatedSnapshot->GetDeserializedActor(OriginalActorPath);
		if (!ensureAlwaysMsgf(DeserializedSnapshotActor.Get(nullptr), TEXT("Failed to get TMap value for key %s. Is the snapshot corrupted?"), *OriginalActorPath.ToString()))
		{
			return;
		}
        	
		if (RelatedSnapshot->HasOriginalChangedPropertiesSinceSnapshotWasTaken(*DeserializedSnapshotActor, WorldActor))
		{
			const EFilterResult::Type ActorInclusionResult = FilterToApply->IsActorValid(FIsActorValidParams(*DeserializedSnapshotActor, WorldActor));
			if (EFilterResult::CanInclude(ActorInclusionResult))
			{
				ModifiedFilteredActors.Add(WorldActor);
			}
		}
	}
}

void FFilterListData::HandleActorWasRemovedFromWorld(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply)
{
	const EFilterResult::Type FilterResult = FilterToApply->IsDeletedActorValid(
		FIsDeletedActorValidParams(
			OriginalActorPath,
			[this](const FSoftObjectPath& ObjectPath)
			{
				return RelatedSnapshot->GetDeserializedActor(ObjectPath).Get(nullptr);
			}
		)
	);
	if (EFilterResult::CanInclude(FilterResult))
	{
		FilteredRemovedOriginalActorPaths.Add(OriginalActorPath);
	}
}

void FFilterListData::HandleActorWasAddedToWorld(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply)
{
	const EFilterResult::Type FilterResult = FilterToApply->IsAddedActorValid(FIsAddedActorValidParams(WorldActor)); 
	if (EFilterResult::CanInclude(FilterResult))
	{
		FilteredAddedWorldActors.Add(WorldActor);
	}
}

#undef LOCTEXT_NAMESPACE