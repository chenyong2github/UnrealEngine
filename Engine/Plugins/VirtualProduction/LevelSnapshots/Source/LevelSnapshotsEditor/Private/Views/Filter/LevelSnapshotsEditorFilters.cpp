// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Filter/LevelSnapshotsEditorFilters.h"

#include "Views/Filter/SLevelSnapshotsEditorFilters.h"
#include "LevelSnapshotFilters.h"

FLevelSnapshotsEditorFilters::FLevelSnapshotsEditorFilters(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
	: BuilderPtr(InBuilder)
{
}

TSharedRef<SWidget> FLevelSnapshotsEditorFilters::GetOrCreateWidget()
{
	if (!EditorFiltersWidget.IsValid())
	{
		SAssignNew(EditorFiltersWidget, SLevelSnapshotsEditorFilters, SharedThis(this));
	}

	return EditorFiltersWidget.ToSharedRef();
}

FLevelSnapshotsEditorFilters::FOnSetActiveFilter& FLevelSnapshotsEditorFilters::GetOnSetActiveFilter()
{
	return OnSetActiveFilter;
}

void FLevelSnapshotsEditorFilters::SetActiveFilter(ULevelSnapshotFilter* InFilter)
{
	ActiveFilterPtr = InFilter;

	OnSetActiveFilter.Broadcast(InFilter);
}

const ULevelSnapshotFilter* FLevelSnapshotsEditorFilters::GetActiveFilter() const
{
	return ActiveFilterPtr.Get();
}
