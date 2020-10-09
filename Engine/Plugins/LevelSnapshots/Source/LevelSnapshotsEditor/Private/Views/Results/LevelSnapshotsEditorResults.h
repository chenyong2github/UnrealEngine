// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorResults.h"

class SLevelSnapshotsEditorResults;

class FLevelSnapshotsEditorResults : public ILevelSnapshotsEditorResults
{
public:
	FLevelSnapshotsEditorResults(const FLevelSnapshotsEditorViewBuilder& InBuilder);

	//~ Begin ILevelSnapshotsEditorView Interface
	virtual TSharedRef<SWidget> GetOrCreateWidget() override;
	virtual const FLevelSnapshotsEditorViewBuilder& GetBuilder() const override { return Builder; }
	//~ End ILevelSnapshotsEditorView Interface

private:
	TSharedPtr<SLevelSnapshotsEditorResults> EditorResultsWidget;
	FLevelSnapshotsEditorViewBuilder Builder;
};
