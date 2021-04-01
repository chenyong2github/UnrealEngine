// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilteredResults.h"

#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsStats.h"

#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "PropertyInfoHelpers.h"
#include "Stats/StatsMisc.h"

UFilteredResults::UFilteredResults(const FObjectInitializer& ObjectInitializer)
{
	PropertiesToRollback = ObjectInitializer.CreateDefaultSubobject<ULevelSnapshotSelectionSet>(
		this,
		TEXT("PropertiesToRollback")
		);
}

void UFilteredResults::CleanReferences()
{
	FilteredData = FFilterListData();
	PropertiesToRollback->Clear();
}

void UFilteredResults::SetActiveLevelSnapshot(ULevelSnapshot* InActiveLevelSnapshot)
{
	UserSelectedSnapshot = InActiveLevelSnapshot;
	CleanReferences();
}

void UFilteredResults::SetUserFilters(ULevelSnapshotFilter* InUserFilters)
{
	UserFilters = InUserFilters;
}

void UFilteredResults::UpdateFilteredResults()
{
	if (!ensure(UserSelectedSnapshot.IsValid()) || !ensure(UserFilters.IsValid()) || !ensure(SelectedWorld.IsValid()))
	{
		return;
	}

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UpdateFilteredResults"), STAT_UpdateFilteredResults, STATGROUP_LevelSnapshots);
	CleanReferences();

	ULevelSnapshot* ActiveSnapshot = UserSelectedSnapshot.Get();
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<AActor>> ModifiedWorldActorToDeserializedSnapshotActor;
	TSet<TWeakObjectPtr<AActor>> ModifiedFilteredActors;
	TSet<TWeakObjectPtr<AActor>> UnmodifiedUnfilteredActors;

	UserSelectedSnapshot->ForEachOriginalActor([this, ActiveSnapshot, &ModifiedWorldActorToDeserializedSnapshotActor, &ModifiedFilteredActors, &UnmodifiedUnfilteredActors](const FSoftObjectPath& OriginalActorPath)
	{
        UObject* ResolvedWorldActor = OriginalActorPath.ResolveObject();
        if (!ResolvedWorldActor)
        {
            UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to resolve actor %s. Was it deleted from the world?"), *OriginalActorPath.ToString());
			return;
        }

        AActor* WorldActor = Cast<AActor>(ResolvedWorldActor);
        if (ensureAlwaysMsgf(WorldActor, TEXT("A path that was previously associated with an actor no longer refers to an actor. Something is wrong.")))
        {
            TOptional<AActor*> DeserializedSnapshotActor = ActiveSnapshot->GetDeserializedActor(OriginalActorPath);
			checkf(DeserializedSnapshotActor.Get(nullptr), TEXT("Failed to get TMap value for key %s. Is the snapshot corrupted?"), *OriginalActorPath.ToString());
        	
            if (ActiveSnapshot->HasOriginalChangedSinceSnapshot(*DeserializedSnapshotActor, WorldActor))
            {
                const EFilterResult::Type ActorInclusionResult = UserFilters->IsActorValid(FIsActorValidParams(*DeserializedSnapshotActor, WorldActor));
                if (EFilterResult::ShouldInclude(ActorInclusionResult))
                {
                    ModifiedFilteredActors.Add(WorldActor);
                    ModifiedWorldActorToDeserializedSnapshotActor.Add({ WorldActor }, { *DeserializedSnapshotActor });
                }
            }
            else
            {
                UnmodifiedUnfilteredActors.Add(WorldActor);
            }
        }
	});

	FilteredData = FFilterListData(ActiveSnapshot, ModifiedWorldActorToDeserializedSnapshotActor, ModifiedFilteredActors, UnmodifiedUnfilteredActors);
}

void UFilteredResults::UpdatePropertiesToRollback(ULevelSnapshotSelectionSet* InSelectionSet)
{
	PropertiesToRollback = InSelectionSet;
}

FFilterListData& UFilteredResults::GetFilteredData()
{
	return FilteredData;
}

TWeakObjectPtr<ULevelSnapshotFilter> UFilteredResults::GetUserFilters() const
{
	return UserFilters;
}

ULevelSnapshotSelectionSet* UFilteredResults::GetSelectionSet() const
{
	return PropertiesToRollback;
}

void UFilteredResults::SetSelectedWorld(UWorld* InWorld)
{
	SelectedWorld = InWorld;
	CleanReferences();
}
