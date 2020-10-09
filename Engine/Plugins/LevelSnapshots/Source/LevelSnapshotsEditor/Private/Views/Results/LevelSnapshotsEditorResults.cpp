// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/LevelSnapshotsEditorResults.h"

#include "Views/Results/SLevelSnapshotsEditorResults.h"

FLevelSnapshotsEditorResults::FLevelSnapshotsEditorResults(const FLevelSnapshotsEditorViewBuilder& InBuilder)
	: Builder(InBuilder)
{
}

TSharedRef<SWidget> FLevelSnapshotsEditorResults::GetOrCreateWidget()
{
	if (!EditorResultsWidget.IsValid())
	{
		SAssignNew(EditorResultsWidget, SLevelSnapshotsEditorResults, SharedThis(this));
	}

	return EditorResultsWidget.ToSharedRef();
}
