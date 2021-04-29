// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilteredResults.h"

#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "LevelSnapshotsStats.h"

#include "EngineUtils.h"
#include "Stats/StatsMisc.h"

void UFilteredResults::CleanReferences()
{
	FilteredData = FFilterListData();
	PropertiesToRollback.Empty();
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
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UpdateFilteredResults"), STAT_UpdateFilteredResults, STATGROUP_LevelSnapshots);
	if (!ensure(UserSelectedSnapshot.IsValid()) || !ensure(UserFilters.IsValid()) || !ensure(SelectedWorld.IsValid()))
	{
		return;
	}
 
	// Do not CleanReferences because we want FilteredData to retain some of the memory it has already allocated
	PropertiesToRollback.Empty(false);
	FilteredData.UpdateFilteredList(SelectedWorld.Get(), UserSelectedSnapshot.Get(), UserFilters.Get());
}

void UFilteredResults::SetPropertiesToRollback(const FPropertySelectionMap& InSelectionSet)
{
	PropertiesToRollback = InSelectionSet;
}

const FPropertySelectionMap& UFilteredResults::GetPropertiesToRollback() const
{
	return PropertiesToRollback;
}

FFilterListData& UFilteredResults::GetFilteredData()
{
	return FilteredData;
}

TWeakObjectPtr<ULevelSnapshotFilter> UFilteredResults::GetUserFilters() const
{
	return UserFilters;
}

void UFilteredResults::SetSelectedWorld(UWorld* InWorld)
{
	SelectedWorld = InWorld;
	CleanReferences();
}
