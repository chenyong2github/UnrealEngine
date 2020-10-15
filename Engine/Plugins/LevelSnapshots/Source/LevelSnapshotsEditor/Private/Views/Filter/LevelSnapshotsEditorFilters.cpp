// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Filter/LevelSnapshotsEditorFilters.h"

#include "Views/Filter/SLevelSnapshotsEditorFilters.h"

FLevelSnapshotsEditorFilters::FLevelSnapshotsEditorFilters(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
	: BuilderPtr(InBuilder)
{
}

TSharedRef<SWidget> FLevelSnapshotsEditorFilters::GetOrCreateWidget()
{
	if (!EditorFiltersWidget.IsValid())
	{
		SAssignNew(EditorFiltersWidget, SLevelSnapshotsEditorFilters);
	}

	return EditorFiltersWidget.ToSharedRef();
}
