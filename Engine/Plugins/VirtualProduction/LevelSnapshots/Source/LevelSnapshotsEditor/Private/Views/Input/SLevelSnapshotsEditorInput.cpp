// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Input/SLevelSnapshotsEditorInput.h"

#include "ILevelSnapshotsEditorView.h"
#include "Views/Input/LevelSnapshotsEditorInput.h"
#include "LevelSnapshotsEditorData.h"
#include "SlevelSnapshotsEditorContextPicker.h"
#include "Widgets/SLevelSnapshotsEditorBrowser.h"

#include "LevelSnapshot.h"
#include "Engine/World.h"
#include "Widgets/SBoxPanel.h"

#include "LevelSnapshotsLog.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorInput::~SLevelSnapshotsEditorInput()
{
	FCoreUObjectDelegates::OnObjectModified.Remove(OnObjectModifiedDelegateHandle);
}

void SLevelSnapshotsEditorInput::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorInput>& InEditorInput, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
{
	EditorInputPtr = InEditorInput;
	BuilderPtr = InBuilder;

	// This is a callback lambda to update the snapshot picker when an level snapshot asset is saved, renamed or deleted.
	// Creating an asset should also trigger this, but "Take Snapshot" won't. The snapshot will need to be saved.
	OnObjectModifiedDelegateHandle = FCoreUObjectDelegates::OnObjectModified.AddLambda([this](UObject* ObjectModified)
	{
		if (ObjectModified->IsA(ULevelSnapshot::StaticClass()) && EditorContextPickerPtr.IsValid())
		{
			OverrideWorld(EditorContextPickerPtr.Get()->GetSelectedWorldSoftPath());
		}
	});

	FSoftObjectPath EditorWorldPath = FSoftObjectPath(SLevelSnapshotsEditorContextPicker::GetEditorWorld());

	ChildSlot
		[
			SAssignNew(EditorInputOuterVerticalBox, SVerticalBox)

			+ SVerticalBox::Slot()
			.Padding(FMargin(0.f, 1.0f))
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SAssignNew(EditorContextPickerPtr, SLevelSnapshotsEditorContextPicker, InBuilder)
					.SelectWorldPath(EditorWorldPath)
					.OnSelectWorldContext(this, &SLevelSnapshotsEditorInput::OverrideWorld)
				]
			]

			+ SVerticalBox::Slot()
			[
				
				SAssignNew(EditorBrowserWidgetPtr, SLevelSnapshotsEditorBrowser, InBuilder)
				.OwningWorldPath(EditorWorldPath)
			]
		];
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
