// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Input/SLevelSnapshotsEditorInput.h"

#include "Data/LevelSnapshotsEditorData.h"
#include "Views/Input/LevelSnapshotsEditorInput.h"
#include "Views/SnapshotEditorViewData.h"
#include "Widgets/SLevelSnapshotsEditorBrowser.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Widgets/SBoxPanel.h"

#include "LevelSnapshotsLog.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorInput::~SLevelSnapshotsEditorInput()
{
	FEditorDelegates::OnMapOpened.Remove(OnMapOpenedDelegateHandle);
}

void SLevelSnapshotsEditorInput::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput, const FSnapshotEditorViewData& InViewBuildData)
{
	ViewBuildData = InViewBuildData;
	check(ViewBuildData.EditorDataPtr.IsValid());


	OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddLambda([this] (const FString& InFileName, const bool bAsTemplate)
	{
		check(ViewBuildData.EditorDataPtr.IsValid());
		OverrideWorld(ViewBuildData.EditorDataPtr->GetEditorWorld());
	});

	ChildSlot
	[
		SAssignNew(EditorInputOuterVerticalBox, SVerticalBox)

		+ SVerticalBox::Slot()
		[
			SAssignNew(EditorBrowserWidgetPtr, SLevelSnapshotsEditorBrowser, InViewBuildData)
			.OwningWorldPath(ViewBuildData.EditorDataPtr->GetEditorWorld())
		]
	];
}

void SLevelSnapshotsEditorInput::OpenLevelSnapshotsDialogWithAssetSelected(const FAssetData& InAssetData) const
{
	EditorBrowserWidgetPtr->SelectAsset(InAssetData);
}

void SLevelSnapshotsEditorInput::OverrideWorld(FSoftObjectPath InNewContextPath)
{
	// Replace the Browser widget with new world context if world and builder pointer valid
	if (!ensure(InNewContextPath.IsValid()))
	{
		UE_LOG(LogLevelSnapshots, Error,
			TEXT("SLevelSnapshotsEditorInput::OverrideWorld: Unable to rebuild Snapshot Browser; InNewContext or BuilderPtr are invalid."));
		return;
	}
	
	if (ensure(EditorInputOuterVerticalBox))
	{
		// Remove the Browser widget then add a new one into the same slot
		EditorInputOuterVerticalBox->RemoveSlot(EditorBrowserWidgetPtr.ToSharedRef());
		
		EditorInputOuterVerticalBox->AddSlot()
		[
			SAssignNew(EditorBrowserWidgetPtr, SLevelSnapshotsEditorBrowser, ViewBuildData)
			.OwningWorldPath(InNewContextPath)
		];
	}
}

#undef LOCTEXT_NAMESPACE
