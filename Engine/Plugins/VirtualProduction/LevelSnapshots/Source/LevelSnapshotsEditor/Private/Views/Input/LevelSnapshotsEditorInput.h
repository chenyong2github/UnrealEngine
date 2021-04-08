// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorInput.h"

class SLevelSnapshotsEditorInput;

class FLevelSnapshotsEditorInput : public ILevelSnapshotsEditorInput
{
public:
	FLevelSnapshotsEditorInput(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

	//~ Begin ILevelSnapshotsEditorView Interface
	virtual TSharedRef<SWidget> GetOrCreateWidget() override;
	virtual TSharedRef<FLevelSnapshotsEditorViewBuilder> GetBuilder() const override { return BuilderPtr.Pin().ToSharedRef(); }
	//~ End ILevelSnapshotsEditorView Interface

private:
	TSharedPtr<SLevelSnapshotsEditorInput> EditorInputWidget;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;
};
