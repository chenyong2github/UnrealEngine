// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/LevelSnapshotsEditorResults.h"

#include "Widgets/SNullWidget.h"

#include "Views/Results/SLevelSnapshotsEditorResults.h"

FLevelSnapshotsEditorResults::FLevelSnapshotsEditorResults(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
	: BuilderPtr(InBuilder)
{
}

TSharedRef<SWidget> FLevelSnapshotsEditorResults::GetOrCreateWidget()
{
	if (!EditorResultsWidget.IsValid())
	{
		TSharedPtr<FLevelSnapshotsEditorViewBuilder> PinnedBuilder = BuilderPtr.Pin();
		if (ensure(PinnedBuilder && PinnedBuilder->EditorDataPtr.IsValid()))
		{
			SAssignNew(EditorResultsWidget, SLevelSnapshotsEditorResults, BuilderPtr.Pin()->EditorDataPtr.Get());
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

	return EditorResultsWidget.ToSharedRef();
}

void FLevelSnapshotsEditorResults::BuildSelectionSetFromSelectedPropertiesInEachActorGroup() const
{
	EditorResultsWidget->BuildSelectionSetFromSelectedPropertiesInEachActorGroup();
}

void FLevelSnapshotsEditorResults::RefreshResults() const
{
	EditorResultsWidget->RefreshResults();
}
