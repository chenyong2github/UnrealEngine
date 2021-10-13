// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Input/LevelSnapshotsEditorInput.h"

#include "Views/Input/SLevelSnapshotsEditorInput.h"

#include "CoreUObject/Public/AssetRegistry/AssetData.h"

TSharedRef<SWidget> FLevelSnapshotsEditorInput::GetOrCreateWidget()
{
	if (!EditorInputWidget.IsValid())
	{
		SAssignNew(EditorInputWidget, SLevelSnapshotsEditorInput, SharedThis(this), ViewBuildData);
	}

	return EditorInputWidget.ToSharedRef();
}

void FLevelSnapshotsEditorInput::OpenLevelSnapshotsDialogWithAssetSelected(const FAssetData& InAssetData) const
{
	EditorInputWidget->OpenLevelSnapshotsDialogWithAssetSelected(InAssetData);
}

void FLevelSnapshotsEditorInput::ShowOrHideInputPanel(const bool bShouldShow)
{
	EditorInputWidget->SetVisibility(bShouldShow ? EVisibility::Visible : EVisibility::Collapsed);
}
