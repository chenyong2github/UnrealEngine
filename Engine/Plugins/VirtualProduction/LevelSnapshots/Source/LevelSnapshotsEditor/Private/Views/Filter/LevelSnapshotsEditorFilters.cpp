// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Filter/LevelSnapshotsEditorFilters.h"

#include "ILevelSnapshotsEditorView.h"
#include "LevelSnapshotFilters.h"
#include "NegatableFilter.h"
#include "Views/Filter/SLevelSnapshotsEditorFilters.h"

FLevelSnapshotsEditorFilters::FLevelSnapshotsEditorFilters(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
	: BuilderPtr(InBuilder)
{}

TSharedRef<SLevelSnapshotsEditorFilters> FLevelSnapshotsEditorFilters::GetOrCreateWidget()
{
	if (!EditorFiltersWidget.IsValid())
	{
		SAssignNew(EditorFiltersWidget, SLevelSnapshotsEditorFilters, SharedThis(this));
	}

	return EditorFiltersWidget.ToSharedRef();
}
