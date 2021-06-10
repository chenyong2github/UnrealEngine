// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorData.h"
#include "SLevelSnapshotsEditorInput.h"

#include "Editor.h"
#include "Engine/World.h"

#include "FavoriteFilterContainer.h"
#include "DisjunctiveNormalFormFilter.h"
#include "FilterLoader.h"
#include "FilteredResults.h"

ULevelSnapshotsEditorData::ULevelSnapshotsEditorData(const FObjectInitializer& ObjectInitializer)
{
	FavoriteFilters = ObjectInitializer.CreateDefaultSubobject<UFavoriteFilterContainer>(
		this,
		TEXT("FavoriteFilters")
		);
	UserDefinedFilters = ObjectInitializer.CreateDefaultSubobject<UDisjunctiveNormalFormFilter>(
		this,
		TEXT("UserDefinedFilters")
		);
	UserDefinedFilters->SetFlags(RF_Transactional);
	
	FilterLoader = ObjectInitializer.CreateDefaultSubobject<UFilterLoader>(
		this,
		TEXT("FilterLoader")
		);
	FilterLoader->SetFlags(RF_Transactional);
	FilterLoader->SetAssetBeingEdited(UserDefinedFilters);
	FilterLoader->OnFilterChanged.AddLambda([this](UDisjunctiveNormalFormFilter* NewFilterToEdit)
	{
		Modify();
		UDisjunctiveNormalFormFilter* OldFilter = UserDefinedFilters;
		UserDefinedFilters = NewFilterToEdit;
		UserDefinedFilters->MarkTransactional();

		FilterResults->Modify();
		FilterResults->SetUserFilters(UserDefinedFilters);
		OnUserDefinedFiltersChanged.Broadcast(NewFilterToEdit, OldFilter);
	});

	FilterResults = ObjectInitializer.CreateDefaultSubobject<UFilteredResults>(
        this,
        TEXT("FilterResults")
        );
	FilterResults->SetUserFilters(UserDefinedFilters);

	OnWorldCleanup = FWorldDelegates::OnWorldCleanup.AddLambda([this](UWorld* World, bool bSessionEnded, bool bCleanupResources)
    {
        ClearActiveSnapshot();
    });
}

void ULevelSnapshotsEditorData::BeginDestroy()
{
	Super::BeginDestroy();
	
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanup);
}

void ULevelSnapshotsEditorData::CleanupAfterEditorClose()
{
	OnActiveSnapshotChanged.Clear();
	OnEditedFiterChanged.Clear();
	OnUserDefinedFiltersChanged.Clear();

	SelectedWorld.Reset();
	ActiveSnapshot.Reset();
	EditedFilter.Reset();

	FilterResults->CleanReferences();
}

void ULevelSnapshotsEditorData::SetActiveSnapshot(const TOptional<ULevelSnapshot*>& NewActiveSnapshot)
{
	ActiveSnapshot = NewActiveSnapshot.Get(nullptr) ? TStrongObjectPtr<ULevelSnapshot>(NewActiveSnapshot.GetValue()) : TOptional<TStrongObjectPtr<ULevelSnapshot>>();

	FilterResults->SetActiveLevelSnapshot(NewActiveSnapshot.Get(nullptr));
	OnActiveSnapshotChanged.Broadcast(GetActiveSnapshot());
}

void ULevelSnapshotsEditorData::ClearActiveSnapshot()
{
	ActiveSnapshot.Reset();
	FilterResults->SetActiveLevelSnapshot(nullptr);
	OnActiveSnapshotChanged.Broadcast(TOptional<ULevelSnapshot*>(nullptr));
}

TOptional<ULevelSnapshot*> ULevelSnapshotsEditorData::GetActiveSnapshot() const
{
	return ActiveSnapshot.IsSet() ? TOptional<ULevelSnapshot*>(ActiveSnapshot->Get()) : TOptional<ULevelSnapshot*>();
}

void ULevelSnapshotsEditorData::SetSelectedWorldReference(UWorld* InWorld)
{
	SelectedWorld = InWorld;
	
	FilterResults->SetSelectedWorld(InWorld);
}

void ULevelSnapshotsEditorData::ClearSelectedWorld()
{
	SelectedWorld = nullptr;
	
	FilterResults->ClearSelectedWorld();
}

UWorld* ULevelSnapshotsEditorData::GetSelectedWorld() const
{
	return SelectedWorld.IsSet() ? SelectedWorld.GetValue() : nullptr;
}

void ULevelSnapshotsEditorData::SetEditedFilter(const TOptional<UNegatableFilter*>& InFilter)
{
	EditedFilter = InFilter.Get(nullptr) ? TStrongObjectPtr<UNegatableFilter>(InFilter.GetValue()) : TOptional<TStrongObjectPtr<UNegatableFilter>>();
	OnEditedFiterChanged.Broadcast(GetEditedFilter());
}

TOptional<UNegatableFilter*> ULevelSnapshotsEditorData::GetEditedFilter() const
{
	return EditedFilter.IsSet() ? TOptional<UNegatableFilter*>(EditedFilter->Get()) : TOptional<UNegatableFilter*>();
}

bool ULevelSnapshotsEditorData::IsEditingFilter(UNegatableFilter* Filter) const
{
	return (Filter == nullptr && !EditedFilter.IsSet()) || (EditedFilter.IsSet() && Filter && Filter == EditedFilter->Get()); 
}

UFavoriteFilterContainer* ULevelSnapshotsEditorData::GetFavoriteFilters() const
{
	return FavoriteFilters;
}

UDisjunctiveNormalFormFilter* ULevelSnapshotsEditorData::GetUserDefinedFilters() const
{
	return UserDefinedFilters;
}

UFilterLoader* ULevelSnapshotsEditorData::GetFilterLoader() const
{
	return FilterLoader;
}

UFilteredResults* ULevelSnapshotsEditorData::GetFilterResults() const
{
	return FilterResults;
}
