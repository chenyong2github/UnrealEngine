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
#include "Widgets/Images/SThrobber.h"


#include "Styling/AppStyle.h"

#include "UGSTab.h"

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

// Button callbacks
TSharedRef<SWidget> SGameSyncTab::MakeSyncButtonDropdown()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("Sync Latest")),
		FText::FromString(TEXT("Sync to the latest submitted changelist")),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				Tab->OnSyncLatest();
			}),
			FCanExecuteAction::CreateLambda([] { return true; })),
		NAME_None,
		EUserInterfaceActionType::Button
		);

	return MenuBuilder.MakeWidget();
}

void SGameSyncTab::Construct(const FArguments& InArgs)
{
	Tab = InArgs._Tab;
	HordeBuilds = InArgs._HordeBuilds;

	this->ChildSlot
	[
		SNew(SVerticalBox)
		// Toolbar at the top of the tab // Todo: Maybe use a FToolBarBuilder instead
		+SVerticalBox::Slot()
		.FillHeight(0.05f)
		// .MaxHeight(35.0f)
		.Padding(20.0f, 5.0f)
		[
			SNew(SBorder)
			.IsEnabled_Lambda([this] { return !Tab->IsSyncing(); })
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
						.MenuContent()
						[
							MakeSyncButtonDropdown()
						]
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
		.FillHeight(0.2f)
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
				// Stream and uproject path
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SVerticalBox) // Todo: Add buttons/dropdowns to each option here
					+SVerticalBox::Slot()
					.Padding(10.0f, 25.0f)
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
							SAssignNew(StreamPathText, STextBlock)
							.Text(LOCTEXT("StreamTextValue", "No stream path found"))
						]
					]
					+SVerticalBox::Slot()
					.Padding(10.0f, 25.0f)
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
							SAssignNew(ProjectPathText, STextBlock)
							.Text(LOCTEXT("ProjectValue", "No project path found"))
						]
					]
				]
				// Syncing files progress
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Top)
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox) // Todo: Only display this widget when syncing
					.Visibility_Lambda([this] { return Tab->IsSyncing() ? EVisibility::Visible : EVisibility::Hidden; })
					+SHorizontalBox::Slot()
					.Padding(5.0f, 10.0f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SyncProgress", "Syncing Files"))
					]
					+SHorizontalBox::Slot()
					.Padding(0.0f, 5.0f, 10.0f, 5.0f)
					.AutoWidth()
					[
						SNew(SThrobber)
					]
				]
			]
		]
		// Horde builds
		+SVerticalBox::Slot()
		.Padding(20.0f, 5.0f)
		.FillHeight(0.45f)
		[
			SAssignNew(HordeBuildsView, SListView<TSharedPtr<HordeBuildRowInfo>>)
			.IsEnabled_Lambda([this] { return !Tab->IsSyncing(); })
			.ListItemsSource(&HordeBuilds)
			.HeaderRow(
				SNew(SHeaderRow)
				+SHeaderRow::Column(FName(TEXT("TYPE")))
				.DefaultLabel(LOCTEXT("HordeHeaderType", "TYPE"))
				+SHeaderRow::Column(FName(TEXT("CHANGE")))
				.DefaultLabel(LOCTEXT("HordeHeaderChange", "CHANGE"))
				+SHeaderRow::Column(FName(TEXT("AUTHOR")))
				.DefaultLabel(LOCTEXT("HordeHeaderAuthor", "AUTHOR"))
				+SHeaderRow::Column(FName(TEXT("DESCRIPTION")))
				.DefaultLabel(LOCTEXT("HordeHeaderDescription", "DESCRIPTION"))
				+SHeaderRow::Column(FName(TEXT("EDITOR")))
				.DefaultLabel(LOCTEXT("HordeHeaderEditor", "EDITOR"))
				+SHeaderRow::Column(FName(TEXT("PLATFORMS")))
				.DefaultLabel(LOCTEXT("HordeHeaderPlatforms", "PLATFORMS"))
				+SHeaderRow::Column(FName(TEXT("CIS")))
				.DefaultLabel(LOCTEXT("HordeHeaderCIS", "CIS"))
				+SHeaderRow::Column(FName(TEXT("STATUS")))
				.DefaultLabel(LOCTEXT("HordeHeaderStatus", "STATUS"))
			)
			.OnGenerateRow(this, &SGameSyncTab::GenerateHordeBuildTableRow)
		]
		// Log
		+SVerticalBox::Slot()
		.Padding(10.0f, 5.0f, 10.0f, 10.0f)
		.FillHeight(0.3f)
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

void SGameSyncTab::SetStreamPathText(FText StreamPath)
{
	fprintf(stderr, "Setting stream path to: %s\n", TCHAR_TO_ANSI(*StreamPath.ToString()));
	StreamPathText->SetText(StreamPath);
}
void SGameSyncTab::SetProjectPathText(FText ProjectPath)
{
	fprintf(stderr, "Setting project path to: %s\n", TCHAR_TO_ANSI(*ProjectPath.ToString()));
	ProjectPathText->SetText(ProjectPath);
}

#undef LOCTEXT_NAMESPACE
