// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Views/SnapshotEditorViewData.h"
#include "Templates/SharedPointer.h"

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

private:
	
	TSharedPtr<SLevelSnapshotsEditorInput> EditorInputWidget;
	FSnapshotEditorViewData ViewBuildData;
};
