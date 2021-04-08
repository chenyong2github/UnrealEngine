// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorResults.h"

class SLevelSnapshotsEditorResults;

class FLevelSnapshotsEditorResults : public ILevelSnapshotsEditorResults
{
public:
	FLevelSnapshotsEditorResults(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

	//~ Begin ILevelSnapshotsEditorView Interface
	virtual TSharedRef<SWidget> GetOrCreateWidget() override;
	virtual TSharedRef<FLevelSnapshotsEditorViewBuilder> GetBuilder() const override { return BuilderPtr.Pin().ToSharedRef(); }
	//~ End ILevelSnapshotsEditorView Interface

	void BuildSelectionSetFromSelectedPropertiesInEachActorGroup() const;
	void RefreshResults() const;

private:
	TSharedPtr<SLevelSnapshotsEditorResults> EditorResultsWidget;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;
};
