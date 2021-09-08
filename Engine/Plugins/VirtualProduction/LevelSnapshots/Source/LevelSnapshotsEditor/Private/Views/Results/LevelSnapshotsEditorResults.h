// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/SnapshotEditorViewData.h"
#include "Templates/SharedPointer.h"

class SLevelSnapshotsEditorResults;

class FLevelSnapshotsEditorResults : public TSharedFromThis<FLevelSnapshotsEditorResults>
{
public:
	
	FLevelSnapshotsEditorResults(const FSnapshotEditorViewData& ViewBuildData)
		: ViewBuildData(ViewBuildData)
	{}

	TSharedRef<SWidget> GetOrCreateWidget();
	const FSnapshotEditorViewData& GetBuilder() const { return ViewBuildData; }

private:
	
	TSharedPtr<SLevelSnapshotsEditorResults> EditorResultsWidget;
	FSnapshotEditorViewData ViewBuildData;
};
