// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Input/LevelSnapshotsEditorInput.h"

#include "Views/Input/SLevelSnapshotsEditorInput.h"

#include "CoreUObject/Public/AssetRegistry/AssetData.h"

FLevelSnapshotsEditorInput::FLevelSnapshotsEditorInput(const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
	: BuilderPtr(InBuilder)
{
}

TSharedRef<SWidget> FLevelSnapshotsEditorInput::GetOrCreateWidget()
{
	if (!EditorInputWidget.IsValid())
	{
		SAssignNew(EditorInputWidget, SLevelSnapshotsEditorInput, SharedThis(this), BuilderPtr.Pin().ToSharedRef());
	}

	return EditorInputWidget.ToSharedRef();
}

void FLevelSnapshotsEditorInput::OpenLevelSnapshotsDialogWithAssetSelected(const FAssetData& InAssetData) const
{
	EditorInputWidget->OpenLevelSnapshotsDialogWithAssetSelected(InAssetData);
}
