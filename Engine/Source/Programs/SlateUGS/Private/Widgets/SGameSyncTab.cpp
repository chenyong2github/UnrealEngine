// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGameSyncTab.h"

#include "Framework/Application/SlateApplication.h"

#include "SPositiveActionButton.h"
#include "SSimpleComboButton.h"
#include "SSimpleButton.h"
#include "Widgets/SLogWidget.h"
#include "Widgets/Layout/SHeader.h"
#include "Widgets/Testing/STestSuite.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Colors/SSimpleGradient.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "UGSWindow"

TSharedRef<ITableRow> SGameSyncTab::GenerateHordeBuildTableRow(TSharedPtr<HordeBuildRowInfo> InItem, const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(STableRow<TSharedPtr<HordeBuildRowInfo>>, InOwnerTable) // Todo: Maybe replace with SMultiColumnTableRow
	[
		SNew(SHorizontalBox)
		// Build status
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding + HordeBuildRowExtraIconPadding, HordeBuildRowVerticalPadding)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"))
			.ColorAndOpacity(InItem->bBuildStatus
				? FLinearColor(116.0f / 255.0f, 160.0f / 255.0f, 64.0f / 255.0f) // Todo: store literals
				: FLinearColor(209.0f / 255.0f, 56.0f / 255.0f, 56.0f / 255.0f))
		]
		// CODE/CONTENT
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CodeContent", "CODE / CONTENT"))
		]
		// Changelist
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(InItem->Changelist)
		]
		// Time
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(InItem->Changelist)
		]
		// Author
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(InItem->Author)
		]
		// Description
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(InItem->Description)
		]
		// EDITOR
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Editor", "EDITOR"))
		]
		// PLATFORMS
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Platforms", "PLATFORMS"))
		]
		// CIS
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CIS", "CIS"))
		]
		// Status
		+SHorizontalBox::Slot()
		.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
		[
			SNew(STextBlock)
			.Text(InItem->Status)
		]
	];
}

void SGameSyncTab::Construct(const FArguments& InArgs)
{
	HordeBuilds = InArgs._HordeBuilds;

	this->ChildSlot
	[
		SNew(SVerticalBox)
		// Toolbar at the top of the tab // Todo: Maybe use a FToolBarBuilder instead
		+SVerticalBox::Slot()
		.MaxHeight(35.0f)
		.Padding(20.0f, 5.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleComboButton)
						.Text(LOCTEXT("Sync", "Sync"))
						.Icon(FAppStyle::Get().GetBrush("Icons.Refresh"))
						.HasDownArrow(true)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("Build", "Build"))
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleComboButton)
						.Text(LOCTEXT("RunUnrealEditor", "Run Unreal Editor"))
						.Icon(FAppStyle::Get().GetBrush("Icons.Launch"))
						.HasDownArrow(true)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("OpenSolution", "OpenSolution"))
						.Icon(FAppStyle::Get().GetBrush("MainFrame.OpenVisualStudio")) // Todo: shouldn't use this icon (repurposing, also could use other IDEs)
					]
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("BuildHealth", "Build Health")) // Todo: What icon?
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("SDKInfo", "SDK Info"))
						.Icon(FAppStyle::Get().GetBrush("Icons.Settings")) // Todo: What icon?
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("OpenPerforce", "OpenPerforce"))
						.Icon(FAppStyle::Get().GetBrush("Icons.Blueprints")) // Todo: shouldn't use this icon (repurposing)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Icon(FAppStyle::Get().GetBrush("GraphEditor.Clean")) // Todo: shouldn't use this icon (repurposing)
						.Text(LOCTEXT("CleanSolution", "Clean Solution"))
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("Filter", "Filter"))
						.Icon(FAppStyle::Get().GetBrush("Icons.Filter"))
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					.HAlign(HAlign_Right)
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("Settings", "Settings"))
						.Icon(FAppStyle::Get().GetBrush("Icons.Settings"))
					]
				]
			]
		]
		// Stream banner 
		+SVerticalBox::Slot()
		.Padding(20.0f, 5.0f)
		.AutoHeight()
		[
			SNew(SOverlay)
			+SOverlay::Slot()
			[
				SNew(SSimpleGradient) // Todo: save literals in class and use different colors depending on stream (Fortnite, UE5, etc)
				.StartColor(FLinearColor(161.0f / 255.0f, 57.0f / 255.0f, 191.0f / 255.0f)) 
				.EndColor(FLinearColor(27.0f / 255.0f, 27.0f / 255.0f, 27.0f / 255.0f))
			]
			+SOverlay::Slot()
			[
				SNew(SHorizontalBox)
				// Stream logo
				+SHorizontalBox::Slot()
				.Padding(20.0f, 10.0f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StreamLogoText", "Fortnite Stream Logo")) // Todo: replace with logo image
					.TextStyle(FAppStyle::Get(), TEXT("Menu.Heading"))
				]
				// Stream, Changelist, uproject path
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SVerticalBox) // Todo: Add buttons/dropdowns to each option here
					+SVerticalBox::Slot()
					.Padding(10.0f, 25.0f)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.Padding(5.0f, 0.0f)
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StreamText", "STREAM"))
						]
						+SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StreamTextValue", "//UE5/Main")) // Todo: replace literal
						]
					]
					+SVerticalBox::Slot()
					.Padding(10.0f, 12.5f)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.Padding(5.0f, 0.0f)
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ChangelistText", "CHANGELIST"))
						]
						+SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ChangelistValue", "14066246")) // Todo: replace literal
						]
					]
					+SVerticalBox::Slot()
					.Padding(10.0f, 25.0f)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.Padding(5.0f, 0.0f)
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ProjectText", "PROJECT"))
						]
						+SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ProjectValue", "/media/robertseiver/DATA1/UE5-Main/QAGame/QAGame.uproject")) // Todo: replace literal
						]
					]
				]
				// Syncing files progress
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Top)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox) // Todo: Only display this widget when syncing
					+SHorizontalBox::Slot()
					.Padding(10.0f, 10.0f)
					.AutoWidth()
					[
						SNew(SImage) // Todo: stop image from being vertically stretched
						.Image(FAppStyle::Get().GetBrush("Icons.Refresh"))
					]
					+SHorizontalBox::Slot()
					.Padding(10.0f, 10.0f)
					.AutoWidth()
					[
						SNew(STextBlock) 
						.Text(LOCTEXT("SyncProgress", "Syncing Files... (85/7827)"))
					]
				]
			]
		]
		// Horde build table header bar
		+SVerticalBox::Slot()
		.MaxHeight(25.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			[
				SNew(SHorizontalBox)
				// CODE/CONTENT
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderType", "TYPE"))
				]
				// Changelist
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderChange", "CHANGE"))
				]
				// Time
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderTime", "TIME"))
				]
				// Author
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderAuthor", "AUTHOR"))
				]
				// Description
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderDescription", "DESCRIPTION"))
				]
				// EDITOR
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderEditor", "EDITOR"))
				]
				// PLATFORMS
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderPlatforms", "PLATFORMS"))
				]
				// CIS
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderCIS", "CIS"))
				]
				// Status
				+SHorizontalBox::Slot()
				.Padding(HordeBuildRowHorizontalPadding, HordeBuildRowVerticalPadding)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HordeHeaderStatus", "STATUS"))
				]
			]
		]
		// Horde builds
		+SVerticalBox::Slot()
		.Padding(20.0f, 5.0f)
		[
			SAssignNew(HordeBuildsView, SListView<TSharedPtr<HordeBuildRowInfo>>)
			.ListItemsSource(&HordeBuilds)
			.OnGenerateRow(this, &SGameSyncTab::GenerateHordeBuildTableRow)
		]
		+SVerticalBox::Slot()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 8.0f)
				[
					SNew(SHeader)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Log", "Log"))
					]
				]
			+SVerticalBox::Slot()
				[
					SAssignNew(SyncLog, SLogWidget)
				]
		]
	];
}

TSharedPtr<SLogWidget> SGameSyncTab::GetSyncLog() const
{
	return SyncLog;
}

void SGameSyncTab::SetSyncLogLocation(const FString& LogFileName)
{
    SyncLog->OpenFile(*LogFileName);
}

#undef LOCTEXT_NAMESPACE
