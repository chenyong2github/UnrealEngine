// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorInput;
class UWorld;

class SLevelSnapshotsEditorInput : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorInput();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorInput)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput);

private:
	void OverrideWith(UWorld* InNewContext);

private:
	TWeakPtr<FLevelSnapshotsEditorInput> EditorInputPtr;
};