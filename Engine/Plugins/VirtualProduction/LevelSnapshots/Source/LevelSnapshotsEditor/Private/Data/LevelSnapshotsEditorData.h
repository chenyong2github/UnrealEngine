// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/StrongObjectPtr.h"

#include "LevelSnapshot.h"
#include "NegatableFilter.h"
#include "LevelSnapshotsEditorData.generated.h"

class SLevelSnapshotsEditorContextPicker;
class UDisjunctiveNormalFormFilter;
class UFavoriteFilterContainer;
class UFilterLoader;
class UFilteredResults;
class ULevelSnapshot;

/* Stores all data shared across the editor's UI. */
UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorData : public UObject
{
	GENERATED_BODY()
public:

	ULevelSnapshotsEditorData(const FObjectInitializer& ObjectInitializer);

	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface
	
	/* Called when the editor is about to be closed. Clears all pending subscriptions to avoid any memory leaks. */
	void CleanupAfterEditorClose();

	
	/******************** Active snapshot ********************/
	void SetActiveSnapshot(const TOptional<ULevelSnapshot*>& NewActiveSnapshot);
	void ClearActiveSnapshot();
	TOptional<ULevelSnapshot*> GetActiveSnapshot() const;

	DECLARE_EVENT_OneParam(ULevelSnapshotsEditorData, FOnActiveSnapshotChanged, const TOptional<ULevelSnapshot*>& /* NewSnapshot */);
	FOnActiveSnapshotChanged OnActiveSnapshotChanged;


	/******************** Selected world ********************/
	void SetSelectedWorldReference(UWorld* InWorld);
	void ClearSelectedWorld();
	UWorld* GetSelectedWorld() const;


	
	/******************** Edited filter ********************/
	void SetEditedFilter(const TOptional<UNegatableFilter*>& InFilter);
	TOptional<UNegatableFilter*> GetEditedFilter() const;
	bool IsEditingFilter(UNegatableFilter* Filter) const;

	DECLARE_EVENT_OneParam(ULevelSnapshotsEditorData, FOnEditedFilterChanged, const TOptional<UNegatableFilter*>& /*NewEditedFilter*/);
	FOnEditedFilterChanged OnEditedFiterChanged;

	DECLARE_EVENT(ULevelSnapshotsEditorData, FOnRefreshResults);
	FOnRefreshResults OnRefreshResults;

	
	/******************** Loading filter preset ********************/
	DECLARE_EVENT_TwoParams(ULevelSnapshotsEditorData, FUserDefinedFiltersChanged, UDisjunctiveNormalFormFilter* /*NewFilter*/, UDisjunctiveNormalFormFilter* /* OldFilter */);
	/* Called when user loads a new set of filters. */
	FUserDefinedFiltersChanged OnUserDefinedFiltersChanged;
	
	
	/******************** Getters ********************/
	UFavoriteFilterContainer* GetFavoriteFilters() const;
	UDisjunctiveNormalFormFilter* GetUserDefinedFilters() const;
	UFilterLoader* GetFilterLoader() const;
	UFilteredResults* GetFilterResults() const;	
	
private:

	FDelegateHandle OnWorldCleanup;
	
	UPROPERTY()
	UFavoriteFilterContainer* FavoriteFilters;
	/* Stores user-defined filters in chain of ORs of ANDs. */
	UPROPERTY()
	UDisjunctiveNormalFormFilter* UserDefinedFilters;
	/* Handles save & load requests for exchanging UserDefinedFilters. */
	UPROPERTY()
	UFilterLoader* FilterLoader;

	/* Converts UserDefinedFilters into ULevelSnapshotsSelectionSet display in results view. */
	UPROPERTY()
	UFilteredResults* FilterResults;

	/* World Picker Reference */
	TOptional<UWorld*> SelectedWorld;

	/* Snapshot selected by user */
	TOptional<TStrongObjectPtr<ULevelSnapshot>> ActiveSnapshot;
	/* Filter visible in details panel */
	TOptional<TStrongObjectPtr<UNegatableFilter>> EditedFilter;
};