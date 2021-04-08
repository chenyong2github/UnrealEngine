// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LevelSnapshotFilters.h"

struct FLevelSnapshotsEditorViewBuilder;
class SLevelSnapshotsEditorFilters;
class UNegatableFilter;

class FLevelSnapshotsEditorFilters : public TSharedFromThis<FLevelSnapshotsEditorFilters>
{
public:

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSetActiveFilter, ULevelSnapshotFilter* /* bInEnabled */);
	
	FLevelSnapshotsEditorFilters(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

	TSharedRef<SLevelSnapshotsEditorFilters> GetOrCreateWidget();
	TSharedPtr<FLevelSnapshotsEditorViewBuilder> GetBuilder() const { return BuilderPtr.Pin(); }

private:
	
	FOnSetActiveFilter OnSetActiveFilter;
	TWeakObjectPtr<ULevelSnapshotFilter> ActiveFilterPtr;
	
	TSharedPtr<SLevelSnapshotsEditorFilters> EditorFiltersWidget;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;
};
