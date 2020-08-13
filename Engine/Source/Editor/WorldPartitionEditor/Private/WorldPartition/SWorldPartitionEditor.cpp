// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/SWorldPartitionEditor.h"
#include "WorldPartition/SWorldPartitionEditorGrid.h"
#include "GameFramework/WorldSettings.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldBrowserModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

void SWorldPartitionEditor::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(ContentParent, SBorder)
		.BorderImage(FEditorStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
	];

	OnBrowseWorld(InArgs._InWorld);

	FWorldBrowserModule& WorldBrowserModule = FModuleManager::LoadModuleChecked<FWorldBrowserModule>("WorldBrowser");
	WorldBrowserModule.OnBrowseWorld.AddSP(this, &SWorldPartitionEditor::OnBrowseWorld);
}

SWorldPartitionEditor::~SWorldPartitionEditor()
{
	FWorldBrowserModule& WorldBrowserModule = FModuleManager::GetModuleChecked<FWorldBrowserModule>("WorldBrowser");
	WorldBrowserModule.OnBrowseWorld.RemoveAll(this);
	
	if (World)
	{
		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			check(WorldPartition->World == World);
			check(WorldPartition->WorldPartitionEditor == this);
			WorldPartition->WorldPartitionEditor = nullptr;
		}
	}
}

void SWorldPartitionEditor::InvalidatePartition()
{
	GridView.Get()->InvalidatePartition();
}

void SWorldPartitionEditor::RecreatePartition()
{
	ContentParent->SetContent(ConstructContentWidget());
}

void SWorldPartitionEditor::Refresh()
{
	GridView->RefreshSceneOutliner();
}

void SWorldPartitionEditor::OnBrowseWorld(UWorld* InWorld)
{
	World = InWorld;
	RecreatePartition();

	// No need to unregister as the previous UWorldPartitionSubsystem is already destroyed
	WorldPartitionChangedDelegateHandle.Reset();
}

TSharedRef<SWidget> SWorldPartitionEditor::ConstructContentWidget()
{
	FName EditorName = NAME_None;	
	if (World)
	{
		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			check(WorldPartition->World == World);
			EditorName = WorldPartition->GetWorldPartitionEditorName();
			WorldPartition->WorldPartitionEditor = this;
		}
	}

	SWorldPartitionEditorGrid::PartitionEditorGridCreateInstanceFunc PartitionEditorGridCreateInstanceFunc = SWorldPartitionEditorGrid::GetPartitionEditorGridCreateInstanceFunc(EditorName);

	TSharedRef<SWidget> Result = 
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.FillHeight(1)
		[
			SNew(SOverlay)

			// Grid view
			+SOverlay::Slot()
			[
				PartitionEditorGridCreateInstanceFunc(GridView, World)
			]

			// Grid view top status bar
			+SOverlay::Slot()
			.VAlign(VAlign_Top)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush(TEXT("Graph.TitleBackground")))
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
					]
				]
			]

			// Grid view bottom status bar
			+SOverlay::Slot()
			.VAlign(VAlign_Bottom)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush(TEXT("Graph.TitleBackground")))
				[
					SNew(SVerticalBox)

					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
					]
				]
			]
		];

	return Result;
}

#undef LOCTEXT_NAMESPACE