// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/SnapshotEditorViewData.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FLevelSnapshotsEditorInput;
class SLevelSnapshotsEditorBrowser;
class SLevelSnapshotsEditorContextPicker;
class SVerticalBox;
class UWorld;

struct FSnapshotEditorViewData;

class SLevelSnapshotsEditorInput : public SCompoundWidget
{
public:

	~SLevelSnapshotsEditorInput();
	
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorInput)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput, const FSnapshotEditorViewData& InViewBuildData);

	void OpenLevelSnapshotsDialogWithAssetSelected(const FAssetData& InAssetData) const;

private:
	
	void OverrideWorld(FSoftObjectPath InNewContextPath);

	FDelegateHandle OnMapOpenedDelegateHandle;
	FSnapshotEditorViewData ViewBuildData;

	TSharedPtr<SVerticalBox> EditorInputOuterVerticalBox;
	TSharedPtr<SLevelSnapshotsEditorContextPicker> EditorContextPickerPtr;
	TSharedPtr<SLevelSnapshotsEditorBrowser> EditorBrowserWidgetPtr;

};