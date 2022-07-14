// Copyright Epic Games, Inc. All Rights Reserved.

#include "SGameSyncTab.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "SPositiveActionButton.h"
#include "SSimpleComboButton.h"
#include "SSimpleButton.h"
#include "SSyncFilterWindow.h"
#include "Widgets/SLogWidget.h"
#include "Widgets/Layout/SHeader.h"
#include "Widgets/Testing/STestSuite.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Colors/SSimpleGradient.h"
#include "Widgets/Images/SThrobber.h"

#include "Styling/AppStyle.h"

#include "UGSTab.h"

#include "Math/RandomStream.h" // Todo: Delete

#define LOCTEXT_NAMESPACE "UGSWindow"

namespace
{
	const FName HordeTableColumnStatus(TEXT("Status"));
	const FName HordeTableColumnChange(TEXT("Change"));
	const FName HordeTableColumnTime(TEXT("Time"));
	const FName HordeTableColumnAuthor(TEXT("Author"));
	const FName HordeTableColumnDescription(TEXT("Description"));
}

void SBuildDataRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FChangeInfo>& Item)
{
	CurrentItem = Item;
	SMultiColumnTableRow<TSharedPtr<FChangeInfo>>::Construct(SMultiColumnTableRow::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SBuildDataRow::GenerateWidgetForColumn(const FName& ColumnId) // Todo: maybe can refactor some of this code so there's less duplication by using the root SWidget class on different types
{
	if (ColumnId == HordeTableColumnStatus)
	{
		TSharedRef<SImage> StatusCircle = SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"));

		switch (CurrentItem->ReviewStatus)
		{
			case EReviewVerdict::Good:
				StatusCircle->SetColorAndOpacity(FLinearColor::Green);
				break;
			case EReviewVerdict::Bad:
				StatusCircle->SetColorAndOpacity(FLinearColor::Red);
				break;
			case EReviewVerdict::Mixed:
				StatusCircle->SetColorAndOpacity(FLinearColor::Yellow);
				break;
			case EReviewVerdict::Unknown:
			default:
				StatusCircle->SetColorAndOpacity(FLinearColor::Gray);
				break;
		}

		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				StatusCircle
			];
	}

	TSharedRef<STextBlock> TextItem = SNew(STextBlock);
	if (ColumnId == HordeTableColumnChange)
	{
		TextItem->SetText(FText::FromString(FString::FromInt(CurrentItem->Changelist)));
		TextItem->SetJustification(ETextJustify::Center);
	}
	if (ColumnId == HordeTableColumnTime)
	{
		TextItem->SetText(CurrentItem->Time);
		TextItem->SetJustification(ETextJustify::Center);
	}
	if (ColumnId == HordeTableColumnAuthor)
	{
		TextItem->SetText(CurrentItem->Author);
	}
	if (ColumnId == HordeTableColumnDescription)
	{
		TextItem->SetText(CurrentItem->Description);
	}

	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(10.0f, 2.5f)
		[
			TextItem
		];
}

TSharedRef<ITableRow> SGameSyncTab::GenerateHordeBuildTableRow(TSharedPtr<FChangeInfo> InItem, const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(SBuildDataRow, InOwnerTable, InItem);
}

// Button callbacks
TSharedRef<SWidget> SGameSyncTab::MakeSyncButtonDropdown()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	MenuBuilder.AddMenuEntry(
		FText::FromString(TEXT("Sync Latest")),
		FText::FromString(TEXT("Sync to the latest submitted changelist")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(Tab, &UGSTab::OnSyncLatest)),
		NAME_None,
		EUserInterfaceActionType::Button
		);

	return MenuBuilder.MakeWidget();
}

void SGameSyncTab::Construct(const FArguments& InArgs)
{
	Tab = InArgs._Tab;

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
						.Text(LOCTEXT("OpenPerforce", "Open Perforce"))
						.Icon(FAppStyle::Get().GetBrush("Icons.Blueprints")) // Todo: shouldn't use this icon (repurposing)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("CleanSolution", "Clean Solution"))
						.Icon(FAppStyle::Get().GetBrush("GraphEditor.Clean")) // Todo: shouldn't use this icon (repurposing)
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSimpleButton)
						.Text(LOCTEXT("Filter", "Filter"))
						.Icon(FAppStyle::Get().GetBrush("Icons.Filter"))
						.OnClicked_Lambda([this] { FSlateApplication::Get().AddModalWindow(SNew(SSyncFilterWindow).Tab(Tab), Tab->GetTabArgs().GetOwnerWindow(), false); return FReply::Handled(); })
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
					.Padding(10.0f, 12.5f)
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
							SAssignNew(ChangelistText, STextBlock)
							.Text(LOCTEXT("ChangelistTextValue", "No changelist found"))
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
					.Padding(5.0f, 25.0f)
					.AutoWidth()
					[
						SAssignNew(SyncProgressText, STextBlock)
						.Text(LOCTEXT("SyncProgress", "Syncing Files"))
						.Text_Lambda([this] { return FText::FromString(Tab->GetSyncProgress()); })
					]
					+SHorizontalBox::Slot()
					.Padding(0.0f, 12.5f, 12.5f, 5.0f)
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
			SAssignNew(HordeBuildsView, SListView<TSharedPtr<FChangeInfo>>)
			.ListItemsSource(&HordeBuilds)
			.SelectionMode(ESelectionMode::Single)
			.IsEnabled_Lambda([this] { return !Tab->IsSyncing(); })
			.OnGenerateRow(this, &SGameSyncTab::GenerateHordeBuildTableRow)
			.OnContextMenuOpening(this, &SGameSyncTab::OnRightClickedBuild)
			.HeaderRow(
				SNew(SHeaderRow)
				+SHeaderRow::Column(HordeTableColumnStatus)
				.DefaultLabel(LOCTEXT("HordeHeaderStatus", ""))
				.FixedWidth(35.0f)
				+SHeaderRow::Column(HordeTableColumnChange)
				.DefaultLabel(LOCTEXT("HordeHeaderChange", "Change"))
				.FixedWidth(100.0f)
				+SHeaderRow::Column(HordeTableColumnTime)
				.DefaultLabel(LOCTEXT("HordeHeaderTime", "Time"))
				.FillWidth(0.2f)
				+SHeaderRow::Column(HordeTableColumnAuthor)
				.DefaultLabel(LOCTEXT("HordeHeaderAuthor", "Author"))
				.FillWidth(0.15f)
				+SHeaderRow::Column(HordeTableColumnDescription)
				.DefaultLabel(LOCTEXT("HordeHeaderDescription", "Description"))
			)
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
	StreamPathText->SetText(StreamPath);
}

void SGameSyncTab::SetChangelistText(FText Changelist)
{
	ChangelistText->SetText(Changelist);
}

void SGameSyncTab::SetProjectPathText(FText ProjectPath)
{
	ProjectPathText->SetText(ProjectPath);
}

void SGameSyncTab::AddHordeBuilds(const TArray<TSharedPtr<FChangeInfo>>& Builds)
{
	HordeBuilds = Builds;
	HordeBuildsView->RebuildList();
}

TSharedPtr<SWidget> SGameSyncTab::OnRightClickedBuild()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	TArray<TSharedPtr<FChangeInfo>> SelectedItems = HordeBuildsView->GetSelectedItems(); // Todo: since I disabled multi select, I might be able to assume the array size is always 1?
	if (SelectedItems.IsValidIndex(0))
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(TEXT("Sync")),
			FText::FromString("Sync to CL " + FString::FromInt(SelectedItems[0]->Changelist)),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(Tab, &UGSTab::OnSyncChangelist, SelectedItems[0]->Changelist)),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
