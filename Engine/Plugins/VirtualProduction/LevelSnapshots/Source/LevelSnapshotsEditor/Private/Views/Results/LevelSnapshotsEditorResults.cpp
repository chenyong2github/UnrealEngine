// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/LevelSnapshotsEditorResults.h"

#include "Widgets/SNullWidget.h"
#include "Views/Results/SLevelSnapshotsEditorResults.h"

TSharedRef<SWidget> FLevelSnapshotsEditorResults::GetOrCreateWidget()
{
	if (!EditorResultsWidget.IsValid())
	{
		if (ensure(ViewBuildData.EditorDataPtr.IsValid()))
		{
			SAssignNew(EditorResultsWidget, SLevelSnapshotsEditorResults, ViewBuildData.EditorDataPtr.Get());
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

	return EditorResultsWidget.ToSharedRef();
}
