// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Input/SLevelSnapshotsEditorInput.h"

#include "Editor.h"
#include "ILevelSnapshotsEditorView.h"
#include "Views/Input/LevelSnapshotsEditorInput.h"
#include "LevelSnapshotsEditorData.h"
#include "Widgets/SLevelSnapshotsEditorBrowser.h"

#include "LevelSnapshot.h"
#include "Engine/World.h"
#include "Widgets/SBoxPanel.h"

#include "LevelSnapshotsLog.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorInput::~SLevelSnapshotsEditorInput()
{
	FEditorDelegates::OnMapOpened.Remove(OnMapOpenedDelegateHandle);
}

void SLevelSnapshotsEditorInput::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
{
	EditorInputPtr = InEditorInput;
	BuilderPtr = InBuilder;

	check(BuilderPtr.Pin()->EditorDataPtr.IsValid());

	OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddLambda([this] (const FString& InFileName, const bool bAsTemplate)
	{
		check(BuilderPtr.IsValid());
		check(BuilderPtr.Pin()->EditorDataPtr.IsValid());
	
		OverrideWorld(BuilderPtr.Pin()->EditorDataPtr->GetEditorWorld());
	});

	ChildSlot
	[
		SAssignNew(EditorInputOuterVerticalBox, SVerticalBox)

		+ SVerticalBox::Slot()
		[
			SAssignNew(EditorBrowserWidgetPtr, SLevelSnapshotsEditorBrowser, InBuilder)
			.OwningWorldPath(BuilderPtr.Pin()->EditorDataPtr->GetEditorWorld())
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
	if (!ensure(InNewContextPath.IsValid()) || !ensure(BuilderPtr.IsValid()))
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
			SAssignNew(EditorBrowserWidgetPtr, SLevelSnapshotsEditorBrowser, BuilderPtr.Pin().ToSharedRef())
			.OwningWorldPath(InNewContextPath)
		];
	}
}

#undef LOCTEXT_NAMESPACE
