// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterListData.h"

#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "LevelSnapshotsStats.h"

FUnmodifiedActors::FUnmodifiedActors(const TSet<TWeakObjectPtr<AActor>>& UnmodifiedActors)
	: UnmodifiedActors(UnmodifiedActors)
{}

bool FUnmodifiedActors::NeedsToApplyFilter() const
{
	return UnmodifiedActors.Num() != 0 && IncludedByFilter.Num() == 0 && ExcludedByFilter.Num() == 0;
}

void FUnmodifiedActors::ApplyFilterToBuildInclusionSet(ULevelSnapshotFilter* FilterToApply)
{
	if (!ensure(FilterToApply))
	{
		return;
	}
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ApplyFilterToBuildInclusionSet"), STAT_ApplyFilterToBuildInclusionSet, STATGROUP_LevelSnapshots);
	
	IncludedByFilter.Reset();
	ExcludedByFilter.Reset();

	for (const TWeakObjectPtr<AActor>& UnmodifiedWorldActor : UnmodifiedActors)
	{
		if (UnmodifiedWorldActor.IsValid())
		{
			AActor* WorldActor = UnmodifiedWorldActor.Get();
			// We know world and snapshot versions are the same: we save time by substituting the snapshot actor with the world actor
			AActor* FakeSnapshotDeserializedObject = WorldActor;
			const FIsActorValidParams Params(FakeSnapshotDeserializedObject, WorldActor);

			const bool bShouldInclude = EFilterResult::ShouldInclude(FilterToApply->IsActorValid(Params));
			if (bShouldInclude)
			{
				IncludedByFilter.Add(UnmodifiedWorldActor);
			}
			else
			{
				ExcludedByFilter.Add(UnmodifiedWorldActor);
			}
		}
	}
}

TSet<TWeakObjectPtr<AActor>> FUnmodifiedActors::GetIncludedByFilter() const
{
	return IncludedByFilter;
}

TSet<TWeakObjectPtr<AActor>> FUnmodifiedActors::GetExcludedByFilter() const
{
	return ExcludedByFilter;
}

TSet<TWeakObjectPtr<AActor>> FUnmodifiedActors::GetUnmodifiedActors() const
{
	return UnmodifiedActors;
}

FFilterListData::FFilterListData(
	ULevelSnapshot* RelatedSnapshot,
	const TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<AActor>>& ModifiedWorldActorToDeserializedSnapshotActor,
	const TSet<TWeakObjectPtr<AActor>>& ModifiedActors,
	const TSet<TWeakObjectPtr<AActor>>& UnmodifiedActors)
	:
	RelatedSnapshot(RelatedSnapshot),
	ModifiedWorldActorToDeserializedSnapshotActor(ModifiedWorldActorToDeserializedSnapshotActor),
	ModifiedFilteredActors(ModifiedActors),
	UnmodifiedUnfilteredActors(FUnmodifiedActors(UnmodifiedActors))
{}

const FPropertySelectionMap& FFilterListData::ApplyFilterToFindSelectedProperties(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply)
{
	if (!ensure(WorldActor && FilterToApply))
	{
		return UnmodifiedActorsSelectedProperties; // Return anything here. We're in an exceptional state anyways...
	}
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ApplyFilterToFindSelectedProperties"), STAT_ApplyFilterToFindSelectedProperties, STATGROUP_LevelSnapshots);
	
	if (ModifiedActorsSelectedProperties.GetSelectedProperties(WorldActor))
	{
		return ModifiedActorsSelectedProperties;
	}
	if (UnmodifiedActorsSelectedProperties.GetSelectedProperties(WorldActor))
	{
		return UnmodifiedActorsSelectedProperties;
	}

	TWeakObjectPtr<AActor>* DeserializedActor = ModifiedWorldActorToDeserializedSnapshotActor.Find(WorldActor);
	if (!ensureMsgf(DeserializedActor && DeserializedActor->IsValid(), TEXT("Deserialized actor no longer exists. The snapshot world was deleted but you forgot to respond to it by clearing this data.")))
	{
		return ModifiedActorsSelectedProperties; // Return anything here. We're in an exceptional state anyways...
	}
	
	const bool bIsModifiedActor = DeserializedActor != nullptr;
	if (bIsModifiedActor)
	{
		ULevelSnapshotsFunctionLibrary::ApplyFilterToFindSelectedProperties(RelatedSnapshot, ModifiedActorsSelectedProperties, WorldActor, DeserializedActor->Get(), FilterToApply);
		return ModifiedActorsSelectedProperties;
	}
	else
	{
		// TODO: It is actually pointless to call ApplyFilterToFindSelectedProperties here now because it skips unchanged properties. We need to implement a flag that tells it to include unchanged properties.
		// We know world and snapshot versions are the same: we save time by substituting the snapshot actor with the world actor
		AActor* FakeSnapshotDeserializedObject = WorldActor;
		ULevelSnapshotsFunctionLibrary::ApplyFilterToFindSelectedProperties(RelatedSnapshot, UnmodifiedActorsSelectedProperties, WorldActor, FakeSnapshotDeserializedObject, FilterToApply);
		return UnmodifiedActorsSelectedProperties;
	}
}

TWeakObjectPtr<AActor> FFilterListData::GetSnapshotCounterpartFor(TWeakObjectPtr<AActor> WorldActor) const
{
	const TWeakObjectPtr<AActor>* DeserializedActor = ModifiedWorldActorToDeserializedSnapshotActor.Find(WorldActor);
	if (DeserializedActor != nullptr && DeserializedActor->IsValid())
	{
		return DeserializedActor->Get();
	}

	checkf(GetUnmodifiedUnfilteredActors().GetUnmodifiedActors().Contains(WorldActor), TEXT("Failed to  get snapshot counterpart for an actor. Possible reasons: 1. never called ApplyFilterToFindSelectedProperties on that actor. 2. related snapshot world was destroyed and you did not clear this data."));
	return WorldActor;
}

const FPropertySelectionMap& FFilterListData::GetModifiedActorsSelectedProperties() const
{
	return ModifiedActorsSelectedProperties;
}

const FPropertySelectionMap& FFilterListData::GetUnmodifiedActorsSelectedProperties() const
{
	return UnmodifiedActorsSelectedProperties;
}

const TSet<TWeakObjectPtr<AActor>>& FFilterListData::GetModifiedFilteredActors() const
{
	return ModifiedFilteredActors;
}

const FUnmodifiedActors& FFilterListData::GetUnmodifiedUnfilteredActors() const
{
	return UnmodifiedUnfilteredActors;
}
