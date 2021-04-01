// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/Object.h"
#include "FilterListData.h"
#include "FilteredResults.generated.h"

class ULevelSnapshot;
class ULevelSnapshotFilter;
class ULevelSnapshotSelectionSet;
class ULevelSnapshotsEditorData;

/* Processes user defined filters into a selection set, which the user inspect in the results tab. */
UCLASS()
class UFilteredResults : public UObject
{
	GENERATED_BODY()
public:

	UFilteredResults(const FObjectInitializer& ObjectInitializer);
	void CleanReferences();
	
	void SetActiveLevelSnapshot(ULevelSnapshot* InActiveLevelSnapshot);
	void SetUserFilters(ULevelSnapshotFilter* InUserFilters);

	/* Extracts DeserializedActorsAndDesiredPaths and FilterResults is modified. */  
	void UpdateFilteredResults();

	void UpdatePropertiesToRollback(ULevelSnapshotSelectionSet* InSelectionSet);
	
	FFilterListData& GetFilteredData();
	TWeakObjectPtr<ULevelSnapshotFilter> GetUserFilters() const;
	ULevelSnapshotSelectionSet* GetSelectionSet() const;

	void SetSelectedWorld(UWorld* InWorld);

private:

	TWeakObjectPtr<ULevelSnapshot> UserSelectedSnapshot;

	TWeakObjectPtr<UWorld> SelectedWorld;

	/* Stores partially filtered data for displaying in filter results view. */
	FFilterListData FilteredData;
	
	TWeakObjectPtr<ULevelSnapshotFilter> UserFilters;
	
	/* Null until UpdatePropertiesToRollback is called. */ 
	UPROPERTY()
	ULevelSnapshotSelectionSet* PropertiesToRollback;
	
};
