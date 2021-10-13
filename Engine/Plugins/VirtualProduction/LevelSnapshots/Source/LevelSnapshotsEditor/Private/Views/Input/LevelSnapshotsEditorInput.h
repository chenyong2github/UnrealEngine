// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Views/SnapshotEditorViewData.h"
#include "Widgets/SWidget.h"

class SWidget;
class SLevelSnapshotsEditorInput;

struct FAssetData;

class FLevelSnapshotsEditorInput : public TSharedFromThis<FLevelSnapshotsEditorInput>
{
public:
	
	FLevelSnapshotsEditorInput(const FSnapshotEditorViewData& ViewBuildData)
		: ViewBuildData(ViewBuildData)
	{}

	TSharedRef<SWidget> GetOrCreateWidget();
	const FSnapshotEditorViewData& GetBuilder() const { return ViewBuildData; }

	void OpenLevelSnapshotsDialogWithAssetSelected(const FAssetData& InAssetData) const;

	void ShowOrHideInputPanel(const bool bShouldShow);

private:
	
	TSharedPtr<SLevelSnapshotsEditorInput> EditorInputWidget;
	FSnapshotEditorViewData ViewBuildData;
};
