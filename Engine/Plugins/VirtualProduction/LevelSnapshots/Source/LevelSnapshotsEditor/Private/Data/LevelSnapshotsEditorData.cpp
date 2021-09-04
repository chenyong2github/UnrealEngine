// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorData.h"

#include "FavoriteFilterContainer.h"
#include "FilterLoader.h"
#include "FilteredResults.h"
#include "LevelSnapshotsLog.h"

#include "Editor.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

ULevelSnapshotsEditorData::ULevelSnapshotsEditorData(const FObjectInitializer& ObjectInitializer)
{
	FavoriteFilters = ObjectInitializer.CreateDefaultSubobject<UFavoriteFilterContainer>(
		this,
		TEXT("FavoriteFilters")
		);
	UserDefinedFilters = ObjectInitializer.CreateDefaultSubobject<ULevelSnapshotsFilterPreset>(
		this,
		TEXT("UserDefinedFilters")
		);

	TrackedFilterModifiedHandle = UserDefinedFilters->OnFilterModified.AddUObject(this, &ULevelSnapshotsEditorData::HandleFilterChange);
	
	FilterLoader = ObjectInitializer.CreateDefaultSubobject<UFilterLoader>(
		this,
		TEXT("FilterLoader")
		);
	FilterLoader->SetFlags(RF_Transactional);
	FilterLoader->SetAssetBeingEdited(UserDefinedFilters);
	FilterLoader->OnFilterChanged.AddLambda([this](ULevelSnapshotsFilterPreset* NewFilterToEdit)
	{
		Modify();
		ULevelSnapshotsFilterPreset* OldFilter = UserDefinedFilters;
		UserDefinedFilters = NewFilterToEdit;
		UserDefinedFilters->MarkTransactional();

		FilterResults->Modify();
		FilterResults->SetUserFilters(UserDefinedFilters);
		OnUserDefinedFiltersChanged.Broadcast(NewFilterToEdit, OldFilter);

		OldFilter->OnFilterModified.Remove(TrackedFilterModifiedHandle);
		
		SetIsFilterDirty(true);
	});

	FilterResults = ObjectInitializer.CreateDefaultSubobject<UFilteredResults>(
        this,
        TEXT("FilterResults")
        );
	FilterResults->SetUserFilters(UserDefinedFilters);
}

void ULevelSnapshotsEditorData::PostInitProperties()
{
	Super::PostInitProperties();

	// Class default should not register to global events
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UserDefinedFilters->MarkTransactional();
		OnObjectsEdited = FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &ULevelSnapshotsEditorData::HandleWorldActorsEdited);
		
		OnWorldCleanup = FWorldDelegates::OnWorldCleanup.AddLambda([this](UWorld* World, bool bSessionEnded, bool bCleanupResources)
		{
			ClearActiveSnapshot();
		});
	}
}

void ULevelSnapshotsEditorData::BeginDestroy()
{
	Super::BeginDestroy();

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanup);
		FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectsEdited);
		
		OnWorldCleanup.Reset();
		OnObjectsEdited.Reset();
	}

	check(!OnWorldCleanup.IsValid());
	check(!OnObjectsEdited.IsValid());
}

void ULevelSnapshotsEditorData::CleanupAfterEditorClose()
{
	OnActiveSnapshotChanged.Clear();
	OnEditedFiterChanged.Clear();
	OnUserDefinedFiltersChanged.Clear();

	ActiveSnapshot.Reset();
	EditedFilter.Reset();

	FilterResults->CleanReferences();
}

void ULevelSnapshotsEditorData::SetActiveSnapshot(const TOptional<ULevelSnapshot*>& NewActiveSnapshot)
{
	SCOPED_SNAPSHOT_EDITOR_TRACE(SetActiveSnapshot);
	
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

UWorld* ULevelSnapshotsEditorData::GetEditorWorld()
{
	// If this function is called very early during startup, the initial editor GWorld may not have been created yet!
	const bool bIsEngineInitialised = GEditor && GEditor->GetWorldContexts().Num() > 0;
	if (bIsEngineInitialised)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			return World;
		}
	}
	return nullptr;
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

ULevelSnapshotsFilterPreset* ULevelSnapshotsEditorData::GetUserDefinedFilters() const
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

void ULevelSnapshotsEditorData::HandleFilterChange(EFilterChangeType FilterChangeType)
{
	// Blank rows do not functionally change the filter
	if (FilterChangeType != EFilterChangeType::BlankRowAdded)
	{
		SetIsFilterDirty(true);
	}
}

bool ULevelSnapshotsEditorData::IsFilterDirty() const
{
	return bIsFilterDirty;
}

void ULevelSnapshotsEditorData::SetIsFilterDirty(const bool bNewDirtyState)
{
	bIsFilterDirty = bNewDirtyState;
}

void ULevelSnapshotsEditorData::HandleWorldActorsEdited(UObject* Object)
{
	if (UWorld* World = GetEditorWorld())
	{
		if (Object && Object->IsIn(World))
		{
			SetIsFilterDirty(true);
		}
	}
}
