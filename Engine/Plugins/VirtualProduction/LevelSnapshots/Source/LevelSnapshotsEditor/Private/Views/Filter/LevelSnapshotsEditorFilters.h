// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorFilters.h"

class SLevelSnapshotsEditorFilters;

class FLevelSnapshotsEditorFilters : public ILevelSnapshotsEditorFilters
{
public:
	FLevelSnapshotsEditorFilters(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

	//~ Begin ILevelSnapshotsEditorView Interface
	virtual TSharedRef<SWidget> GetOrCreateWidget() override;
	virtual TSharedRef<FLevelSnapshotsEditorViewBuilder> GetBuilder() const override { return BuilderPtr.Pin().ToSharedRef(); }
	//~ End ILevelSnapshotsEditorView Interface

	//~ Begin ILevelSnapshotsEditorView Interface
	virtual FOnSetActiveFilter& GetOnSetActiveFilter() override;
	virtual void SetActiveFilter(ULevelSnapshotFilter* InFilter) override;
	virtual const ULevelSnapshotFilter* GetActiveFilter() const override;
	//~ End ILevelSnapshotsEditorView Interface

private:
	TSharedPtr<SLevelSnapshotsEditorFilters> EditorFiltersWidget;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;

	FOnSetActiveFilter OnSetActiveFilter;

	TWeakObjectPtr<ULevelSnapshotFilter> ActiveFilterPtr;
};
