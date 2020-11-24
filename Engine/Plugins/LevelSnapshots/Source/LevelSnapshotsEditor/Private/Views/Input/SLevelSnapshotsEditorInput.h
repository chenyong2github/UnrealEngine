// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorInput;
class SLevelSnapshotsEditorBrowser;
class SLevelSnapshotsEditorContextPicker;
class UWorld;
struct FLevelSnapshotsEditorViewBuilder;

class SLevelSnapshotsEditorInput : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorInput)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

	static UWorld* GetEditorWorld();
private:
	void OverrideWorld(FSoftObjectPath InNewContextPath);

private:
	TWeakPtr<FLevelSnapshotsEditorInput> EditorInputPtr;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;

	TSharedPtr<SVerticalBox> EditorInputOuterVerticalBox;
	TSharedPtr<SLevelSnapshotsEditorContextPicker> EditorContextPickerPtr;
	TSharedPtr<SLevelSnapshotsEditorBrowser> EditorBrowserWidgetPtr;

};