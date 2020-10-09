// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSnapshotsEditorInput.h"

class SLevelSnapshotsEditorInput;

class FLevelSnapshotsEditorInput : public ILevelSnapshotsEditorInput
{
public:
	FLevelSnapshotsEditorInput(const FLevelSnapshotsEditorViewBuilder& InBuilder);

	//~ Begin ILevelSnapshotsEditorView Interface
	virtual TSharedRef<SWidget> GetOrCreateWidget() override;
	virtual const FLevelSnapshotsEditorViewBuilder& GetBuilder() const override { return Builder; }
	//~ End ILevelSnapshotsEditorView Interface

private:
	TSharedPtr<SLevelSnapshotsEditorInput> EditorInputWidget;

	FLevelSnapshotsEditorViewBuilder Builder;
};
