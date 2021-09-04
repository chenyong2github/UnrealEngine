// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterListData.h"

#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "LevelSnapshotsLog.h"

#include "GameFramework/Actor.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void FFilterListData::UpdateFilteredList(UWorld* World, ULevelSnapshot* FromSnapshot, ULevelSnapshotFilter* FilterToApply)
{
	SCOPED_SNAPSHOT_EDITOR_TRACE(UpdateFilteredList);
	
	// We only track progress of HandleActorExistsInWorldAndSnapshot because the other two functions are relatively fast in comparison: deserialisation takes much longer.
	const int32 ExpectedAmountOfWork = FromSnapshot->GetNumSavedActors();
	FScopedSlowTask DiffDeserializedActors(ExpectedAmountOfWork, LOCTEXT("DiffingActorsKey", "Diffing actors"));
	DiffDeserializedActors.MakeDialogDelayed(1.f);

    RelatedSnapshot = FromSnapshot;

	// We expect the number of filtered actors & components to stay roughly the same: retain existing memory
	ModifiedActorsSelectedProperties_AllowedByFilter.Empty(false);
	ModifiedWorldActors_AllowedByFilter.Empty(ModifiedWorldActors_AllowedByFilter.Num());
	RemovedOriginalActorPaths_AllowedByFilter.Empty(RemovedOriginalActorPaths_AllowedByFilter.Num());
	AddedWorldActors_AllowedByFilter.Empty(AddedWorldActors_AllowedByFilter.Num());
	
	FromSnapshot->DiffWorld(
		World,
		ULevelSnapshot::FActorPathConsumer::CreateRaw(this, &FFilterListData::HandleActorExistsInWorldAndSnapshot, FilterToApply, &DiffDeserializedActors),
		ULevelSnapshot::FActorPathConsumer::CreateRaw(this, &FFilterListData::HandleActorWasRemovedFromWorld, FilterToApply),
		ULevelSnapshot::FActorConsumer::CreateRaw(this, &FFilterListData::HandleActorWasAddedToWorld, FilterToApply)
		);
}

void FFilterListData::ApplyFilterToFindSelectedProperties(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply)
{
	const FPropertySelection* AllowedSelectedProperties = ModifiedActorsSelectedProperties_AllowedByFilter.GetSelectedProperties(WorldActor);
	const FPropertySelection* DisallowedSelectedProperties = ModifiedActorsSelectedProperties_AllowedByFilter.GetSelectedProperties(WorldActor);
	
	if (!ensure(WorldActor && FilterToApply) || AllowedSelectedProperties || DisallowedSelectedProperties)
	{
		return;
	}
	
	const bool bIsAllowedByFilters = ModifiedWorldActors_AllowedByFilter.Contains(WorldActor);
	const bool bIsDisallowedByFilters = ModifiedWorldActors_DisallowedByFilter.Contains(WorldActor);
	if (!ensureMsgf(bIsAllowedByFilters || bIsDisallowedByFilters, TEXT("You have to call UpdateFilteredList first")))
	{
		return; 
	}

	const TWeakObjectPtr<AActor> DeserializedActor = GetSnapshotCounterpartFor(WorldActor);
	if (!ensureMsgf(DeserializedActor.IsValid(), TEXT("For some reason this actor has no snapshot counterpart... Investigate.")))
	{
		return;
	}
	
	if (bIsAllowedByFilters)
	{
		ULevelSnapshotsFunctionLibrary::ApplyFilterToFindSelectedProperties(RelatedSnapshot, ModifiedActorsSelectedProperties_AllowedByFilter, WorldActor, DeserializedActor.Get(), FilterToApply);
	}
	else
	{
		ULevelSnapshotsFunctionLibrary::ApplyFilterToFindSelectedProperties(RelatedSnapshot, ModifiedActorsSelectedProperties_DisallowedByFilter, WorldActor, DeserializedActor.Get(), FilterToApply);
	}
}

TWeakObjectPtr<AActor> FFilterListData::GetSnapshotCounterpartFor(TWeakObjectPtr<AActor> WorldActor) const
{
	const TOptional<AActor*> DeserializedActor = RelatedSnapshot->GetDeserializedActor(WorldActor.Get());
	return ensureAlwaysMsgf(DeserializedActor, TEXT("Deserialized actor does no exist. Either the snapshots's container world was deleted or the snapshot has no counterpart for this actor"))
		? *DeserializedActor : nullptr;
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
				ModifiedWorldActors_AllowedByFilter.Add(WorldActor);
			}
			else
			{
				ModifiedWorldActors_DisallowedByFilter.Add(WorldActor);
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
		RemovedOriginalActorPaths_AllowedByFilter.Add(OriginalActorPath);
	}
	else
	{
		RemovedOriginalActorPaths_DisallowedByFilter.Add(OriginalActorPath);
	}
}

void FFilterListData::HandleActorWasAddedToWorld(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply)
{
	const EFilterResult::Type FilterResult = FilterToApply->IsAddedActorValid(FIsAddedActorValidParams(WorldActor)); 
	if (EFilterResult::CanInclude(FilterResult))
	{
		AddedWorldActors_AllowedByFilter.Add(WorldActor);
	}
	else
	{
		AddedWorldActors_DisallowedByFilter.Add(WorldActor);
	}
}

#undef LOCTEXT_NAMESPACE