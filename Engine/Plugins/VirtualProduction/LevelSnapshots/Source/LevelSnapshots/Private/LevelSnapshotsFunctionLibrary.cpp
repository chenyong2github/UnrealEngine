// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsFunctionLibrary.h"

#include "ApplySnapshotFilter.h"
#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsStats.h"

#include "EngineUtils.h"

ULevelSnapshot* ULevelSnapshotsFunctionLibrary::TakeLevelSnapshot(const UObject* WorldContextObject, const FName NewSnapshotName, const FString Description)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TakeLevelSnapshot"), STAT_TakeLevelSnapshot, STATGROUP_LevelSnapshots);
	
	UWorld* TargetWorld = nullptr;
	if (WorldContextObject)
	{
		TargetWorld = WorldContextObject->GetWorld();
	}

	if (TargetWorld)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Snapshot taken in World Type - %d"), TargetWorld->WorldType);
	}
	else
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Snapshot taken with no valid World set"));
		return nullptr;
	}

	ULevelSnapshot* NewSnapshot = NewObject<ULevelSnapshot>(GetTransientPackage(), NewSnapshotName);
	NewSnapshot->SetSnapshotName(NewSnapshotName);
	NewSnapshot->SetSnapshotDescription(Description);
	NewSnapshot->SnapshotWorld(TargetWorld);
	return NewSnapshot;
};

void ULevelSnapshotsFunctionLibrary::ApplyFilterToFindSelectedProperties(
	ULevelSnapshot* Snapshot,
	FPropertySelectionMap& MapToAddTo, 
	AActor* WorldActor,
	AActor* DeserializedSnapshotActor,
	const ULevelSnapshotFilter* Filter,
	bool bAllowUnchangedProperties,
    bool bAllowNonEditableProperties)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ApplyFilterToFindSelectedProperties"), STAT_ApplyFilterToFindSelectedProperties, STATGROUP_LevelSnapshots);
	
	FApplySnapshotFilter::Make(Snapshot, DeserializedSnapshotActor, WorldActor, Filter)
		.AllowUnchangedProperties(bAllowUnchangedProperties)
		.AllowNonEditableProperties(bAllowNonEditableProperties)
		.ApplyFilterToFindSelectedProperties(MapToAddTo);
}

void ULevelSnapshotsFunctionLibrary::ApplySnapshotToWorld(UWorld* TargetWorld, ULevelSnapshot* Snapshot, ULevelSnapshotSelectionSet* SelectionSet)
{
	if (ensure(Snapshot))
	{
		Snapshot->ApplySnapshotToWorld(TargetWorld, SelectionSet);
	}
}
