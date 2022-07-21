// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWorldPartitionViewportWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "Editor.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "Styling/AppStyle.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "WorldPartitionViewportWidget"

void SWorldPartitionViewportWidget::Construct(const FArguments& InArgs)
{
	bClickable = InArgs._Clickable;
	
	ChildSlot.Padding(2, 2, 2, 2)
	[
		SNew(SButton)
		.ButtonStyle(bClickable? &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("HoverHintOnly") : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
		.Cursor(EMouseCursor::Default)
		.IsEnabled_Lambda([this]() { return bClickable; })
		.OnClicked_Lambda([]
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
			LevelEditorTabManager->TryInvokeTab(FName("WorldBrowserPartitionEditor"));
			return FReply::Handled();
		})
		.ToolTipText(LOCTEXT("NoRegionsLoadedTooltip", "To load a region, drag select an area in the World Partition map and choose 'Load Region From Selection' from the context menu."))
		.ContentPadding(0.f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(FMargin(2, 1, 0, 1))
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.WarningWithColor"))
					.DesiredSizeOverride(FVector2D(16, 16))
				]
				+ SHorizontalBox::Slot()
				.Padding(FMargin(6, 1, 2, 1))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.FillWidth(1)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoRegionsLoadedText","No regions loaded"))
					.ColorAndOpacity(FAppStyle::Get().GetSlateColor("Colors.Warning"))
				]
			]
		]
	];


}

SWorldPartitionViewportWidget::~SWorldPartitionViewportWidget()
{
}

EVisibility SWorldPartitionViewportWidget::GetVisibility(UWorld* InWorld)
{
	if (InWorld && InWorld->IsPartitionedWorld())
	{
		UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
		if (WorldPartition->IsStreamingEnabled() && !WorldPartition->HasLoadedUserCreatedRegions())
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE