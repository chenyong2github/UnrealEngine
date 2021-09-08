// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Filter/LevelSnapshotsEditorFilters.h"
#include "Views/Filter/SLevelSnapshotsEditorFilters.h"

TSharedRef<SLevelSnapshotsEditorFilters> FLevelSnapshotsEditorFilters::GetOrCreateWidget()
{
	if (!EditorFiltersWidget.IsValid())
	{
		SAssignNew(EditorFiltersWidget, SLevelSnapshotsEditorFilters, SharedThis(this));
	}

	return EditorFiltersWidget.ToSharedRef();
}
