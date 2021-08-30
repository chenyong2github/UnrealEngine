// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorInput;
class SLevelSnapshotsEditorBrowser;
class SLevelSnapshotsEditorContextPicker;
class SVerticalBox;
class UWorld;

struct FLevelSnapshotsEditorViewBuilder;

class SLevelSnapshotsEditorInput : public SCompoundWidget
{
public:

	~SLevelSnapshotsEditorInput();
	
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorInput)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

	void OpenLevelSnapshotsDialogWithAssetSelected(const FAssetData& InAssetData) const;

private:
	void OverrideWorld(FSoftObjectPath InNewContextPath);

	FDelegateHandle OnMapOpenedDelegateHandle;
	
	TWeakPtr<FLevelSnapshotsEditorInput> EditorInputPtr;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;

	TSharedPtr<SVerticalBox> EditorInputOuterVerticalBox;
	TSharedPtr<SLevelSnapshotsEditorContextPicker> EditorContextPickerPtr;
	TSharedPtr<SLevelSnapshotsEditorBrowser> EditorBrowserWidgetPtr;

};