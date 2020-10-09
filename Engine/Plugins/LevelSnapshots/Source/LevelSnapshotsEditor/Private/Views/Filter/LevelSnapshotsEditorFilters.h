// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorFilters.h"

class SLevelSnapshotsEditorFilters;

class FLevelSnapshotsEditorFilters : public ILevelSnapshotsEditorFilters
{
public:
	FLevelSnapshotsEditorFilters(const FLevelSnapshotsEditorViewBuilder& InBuilder);

	//~ Begin ILevelSnapshotsEditorView Interface
	virtual TSharedRef<SWidget> GetOrCreateWidget() override;
	virtual const FLevelSnapshotsEditorViewBuilder& GetBuilder() const override { return Builder; }
	//~ End ILevelSnapshotsEditorView Interface

private:
	TSharedPtr<SLevelSnapshotsEditorFilters> EditorFiltersWidget;

	FLevelSnapshotsEditorViewBuilder Builder;
};
