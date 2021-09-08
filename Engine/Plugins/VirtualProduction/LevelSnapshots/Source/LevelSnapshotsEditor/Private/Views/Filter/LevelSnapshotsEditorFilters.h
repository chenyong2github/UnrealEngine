// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Views/SnapshotEditorViewData.h"

class SLevelSnapshotsEditorFilters;
class UNegatableFilter;

class FLevelSnapshotsEditorFilters : public TSharedFromThis<FLevelSnapshotsEditorFilters>
{
public:
	
	FLevelSnapshotsEditorFilters(const FSnapshotEditorViewData& BuildData)
		: ViewBuildData(BuildData)
	{}

	TSharedRef<SLevelSnapshotsEditorFilters> GetOrCreateWidget();
	const FSnapshotEditorViewData& GetViewBuildData() const { return ViewBuildData; }

private:
	
	TSharedPtr<SLevelSnapshotsEditorFilters> EditorFiltersWidget;
	FSnapshotEditorViewData ViewBuildData;
};
