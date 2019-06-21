// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SDisasterRecovery.h"

#include "IConcertClientWorkspace.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "ConcertFrontendStyle.h"

#define LOCTEXT_NAMESPACE "SDisasterRecovery"

namespace ConcertSessionRecoveryUtils
{
	const FName DateTimeColumnName  = TEXT("DateTime");
	const FName OperationColumnName = TEXT("Operation");
	const FName PackageColumnName   = TEXT("Package");
	const FName SummaryColumnName   = TEXT("Summary");
	const FName DisplayRelativeTimeCheckBoxMenuName = TEXT("DisplayRelativeTime");
}


void SDisasterRecovery::Construct(const FArguments& InArgs, TArray<TSharedPtr<FConcertClientSessionActivity>> InActivities)
{
	ParentWindow = InArgs._ParentWindow;
	Activities = MoveTemp(InActivities);

	auto BuildViewOptions = [this]()
	{
		FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DisplayRelativeTime", "Display Relative Time"),
			LOCTEXT("DisplayRelativeTime_Tooltip", "Displays Time Relative to the Current Time"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SDisasterRecovery::OnViewOptionCheckBoxToggled, ConcertSessionRecoveryUtils::DisplayRelativeTimeCheckBoxMenuName),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return bDisplayRelativeTime; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		return MenuBuilder.MakeWidget();
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.6, 0.6, 0.6)))
		.Padding(0)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.FillHeight(1.0)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SVerticalBox)

					// Search bar.
					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 1)
					[
						SNew(SBox)
						.Padding(FMargin(0, 1, 0, 1))
						[
							SAssignNew(SearchBox, SSearchBox)
							.HintText(LOCTEXT("SearchHint", "Search..."))
							.OnTextChanged(this, &SDisasterRecovery::OnSearchTextChanged)
							.DelayChangeNotificationsWhileTyping(true)
						]
					]

					// Activity List
					+SVerticalBox::Slot()
					[
						SAssignNew(ActivityView, SListView<TSharedPtr<FConcertClientSessionActivity>>)
						.ListItemsSource(&Activities)
						.OnGenerateRow(this, &SDisasterRecovery::OnGenerateActivityRowWidget)
						.SelectionMode(ESelectionMode::Single)
						.AllowOverscroll(EAllowOverscroll::No)
						.HeaderRow(
							SNew(SHeaderRow)

							+SHeaderRow::Column(ConcertSessionRecoveryUtils::DateTimeColumnName)
							.DefaultLabel(LOCTEXT("DateTime", "Date/Time"))
							.ManualWidth(180)

							+SHeaderRow::Column(ConcertSessionRecoveryUtils::OperationColumnName)
							.DefaultLabel(LOCTEXT("Operation", "Operation"))
							.ManualWidth(180)

							+SHeaderRow::Column(ConcertSessionRecoveryUtils::PackageColumnName)
							.DefaultLabel(LOCTEXT("Package", "Package"))
							.ManualWidth(180)

							+SHeaderRow::Column(ConcertSessionRecoveryUtils::SummaryColumnName)
							.DefaultLabel(LOCTEXT("Summary", "Summary")))
					]

					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(2, 2)
					[
						SNew(SSeparator)
					]

					// Status bar/View options.
					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4, 2)
					[
						SNew(SHorizontalBox)

						// Operation count.
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text_Lambda([this]() { return FText::Format(LOCTEXT("OperationCount", "{0} Operations"), Activities.Num()); })
						]

						// Gap Filler
						+SHorizontalBox::Slot()
						.FillWidth(1.0)
						[
							SNew(SSpacer)
						]

						// View option
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SComboButton)
							.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
							.ForegroundColor(FLinearColor::White)
							.ContentPadding(0)
							.OnGetMenuContent_Lambda(BuildViewOptions)
							.HasDownArrow(true)
							.ContentPadding(FMargin(1, 0))
							.ButtonContent()
							[
								SNew(SHorizontalBox)

								+SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(SImage).Image(FEditorStyle::GetBrush("GenericViewButton")) // The eye ball image.
								]

								+SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(2, 0, 0, 0)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(LOCTEXT("ViewOptions", "View Options"))
								]
							]
						]
					]
				]
			]

			// Buttons
			+SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0, 6)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(2.0f, 0.0f))

				+SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.ForegroundColor(FLinearColor::White)
					.ButtonStyle(FEditorStyle::Get(), TEXT("FlatButton.Success"))
					.ToolTipText(LOCTEXT("RecoverTooltip", "Restore your assets to their state at the selected recovery point (inclusive)"))
					.OnClicked(this, &SDisasterRecovery::OnRecoverClicked)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(14, 3))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Recover", "Recover"))
						.Font( FEditorStyle::GetFontStyle( "BoldFont" ) )
						.ShadowOffset( FVector2D( 1.0f, 1.0f ) )
					]
				]

				+SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.ForegroundColor(FLinearColor::White)
					.ButtonStyle(FEditorStyle::Get(), TEXT("FlatButton.Danger"))
					.ToolTipText(LOCTEXT("CancelRecoveryTooltip", "Discard any recoverable data for your assets and continue with their last saved state"))
					.OnClicked(this, &SDisasterRecovery::OnCancelRecoveryClicked)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(14, 3))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Cancel", "Cancel"))
						.Font( FEditorStyle::GetFontStyle( "BoldFont" ) )
						.ShadowOffset( FVector2D( 1.0f, 1.0f ) )
					]
				]
			]
		]
	];

	// Sort the item from the most recent to the oldest.
	Activities.Sort([](const TSharedPtr<FConcertClientSessionActivity>& Lhs, const TSharedPtr<FConcertClientSessionActivity>& Rhs)
	{
		return Lhs->Activity.EventTime.GetTicks() > Rhs->Activity.EventTime.GetTicks();
	});

	ActivityView->RequestListRefresh();

	if (Activities.Num())
	{
		ActivityView->SetItemSelection(Activities[0], true);
	}
}

TSharedRef<ITableRow> SDisasterRecovery::OnGenerateActivityRowWidget(TSharedPtr<FConcertClientSessionActivity> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SDisasterRecoveryActivityRow, Item, OwnerTable)
		.DisplayRelativeTime_Lambda([this](){ return bDisplayRelativeTime; })
		.OnRecoverFunc([this](TSharedPtr<FConcertClientSessionActivity> SelectedItem) { RecoverThrough(SelectedItem); })
		.HighlightText(this, &SDisasterRecovery::HighlightSearchText)
		.RecoverButtonVisibility_Lambda([this, Item]() // The button at the end of the line in the list view.
		{
			TArray<TSharedPtr<FConcertClientSessionActivity>> SelectedItems = ActivityView->GetSelectedItems();
			return SelectedItems.Num() && Item == SelectedItems[0] ? EVisibility::Visible : EVisibility::Hidden;
		});
}

void SDisasterRecovery::OnSearchTextChanged(const FText& InFilterText)
{
	SearchText = InFilterText;
}

FText SDisasterRecovery::HighlightSearchText() const
{
	return SearchText;
}

void SDisasterRecovery::OnViewOptionCheckBoxToggled(const FName ItemName)
{
	if (ItemName == ConcertSessionRecoveryUtils::DisplayRelativeTimeCheckBoxMenuName)
	{
		bDisplayRelativeTime = !bDisplayRelativeTime;
	}
}

void SDisasterRecovery::RecoverThrough(TSharedPtr<FConcertClientSessionActivity> Item)
{
	RecoveryThroughItem = Item;
	DismissWindow();
}

FReply SDisasterRecovery::OnCancelRecoveryClicked()
{
	RecoveryThroughItem.Reset();

	DismissWindow();
	return FReply::Handled();
}

FReply SDisasterRecovery::OnRecoverClicked()
{
	TArray<TSharedPtr<FConcertClientSessionActivity>> SelectedActivities = ActivityView->GetSelectedItems();
	if (SelectedActivities.Num() == 1)
	{
		RecoveryThroughItem = SelectedActivities[0];
	}

	DismissWindow();
	return FReply::Handled();
}

FReply SDisasterRecovery::OnRecoverAllClicked()
{
	if (Activities.Num()) // Something to recover?
	{
		RecoveryThroughItem = Activities[0];
	}

	DismissWindow();
	return FReply::Handled();
}

void SDisasterRecovery::DismissWindow()
{
	if (TSharedPtr<SWindow> ParentWindowPin = ParentWindow.Pin())
	{
		ParentWindowPin->RequestDestroyWindow();
	}
}


void SDisasterRecoveryActivityRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertClientSessionActivity> InItem, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = InItem;
	RecoverButtonVisibility = InArgs._RecoverButtonVisibility;
	DisplayRelativeTime = InArgs._DisplayRelativeTime;
	OnRecoverFunc = InArgs._OnRecoverFunc;
	HighlightText = InArgs._HighlightText;
	AbsoluteDateTime = FText::AsDateTime(InItem->Activity.EventTime); // Cache the absolute time as it will not change.

	// Construct base class
	SMultiColumnTableRow<TSharedPtr<FConcertClientSessionActivity>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SDisasterRecoveryActivityRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<FConcertClientSessionActivity> ItemPin = Item.Pin();

	auto GetPackageName = [](const TStructOnScope<FConcertSyncActivitySummary>& InActivitySummary)
	{
		if (const FConcertSyncPackageActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncPackageActivitySummary>())
		{
			return FText::FromName(Summary->PackageName);
		}

		if (const FConcertSyncTransactionActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncTransactionActivitySummary>())
		{
			return FText::FromName(Summary->PrimaryPackageName);
		}

		return FText::GetEmpty();
	};

	auto GetSummary = [](const TStructOnScope<FConcertSyncActivitySummary>& InActivitySummary)
	{
		if (const FConcertSyncTransactionActivitySummary* TransactionSummary = InActivitySummary.Cast<FConcertSyncTransactionActivitySummary>())
		{
			return TransactionSummary->ToDisplayText(FText::GetEmpty(), true);
		}

		if (const FConcertSyncActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncActivitySummary>())
		{
			return Summary->ToDisplayText(FText::GetEmpty(), true);
		}

		return FText::GetEmpty();
	};

	auto GetOperation = [](const TStructOnScope<FConcertSyncActivitySummary>& InActivitySummary)
	{
		if (const FConcertSyncTransactionActivitySummary* TransactionSummary = InActivitySummary.Cast<FConcertSyncTransactionActivitySummary>())
		{
			return TransactionSummary->TransactionTitle;
		}

		if (const FConcertSyncActivitySummary* PackageSummary = InActivitySummary.Cast<FConcertSyncPackageActivitySummary>())
		{
			return LOCTEXT("SavePackageOperation", "Save Package");
		}

		return FText::GetEmpty();
	};

	if (ColumnName == ConcertSessionRecoveryUtils::DateTimeColumnName)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SDisasterRecoveryActivityRow::FormatEventDateTime)
				.ToolTipText_Lambda([this]() { return DisplayRelativeTime.Get() ? AbsoluteDateTime : LOCTEXT("DateTimeTooltip", "The Event Date/Time"); })
				.HighlightText(HighlightText)
			];
	}
	else if (ColumnName == ConcertSessionRecoveryUtils::PackageColumnName)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(GetPackageName(ItemPin->ActivitySummary))
				.HighlightText(HighlightText)
			];
	}
	else if (ColumnName == ConcertSessionRecoveryUtils::OperationColumnName)
	{
		return SNew(SBox)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(GetOperation(ItemPin->ActivitySummary))
				.HighlightText(HighlightText)
			];
	}
	else
	{
		check(ColumnName == ConcertSessionRecoveryUtils::SummaryColumnName);
		return SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0, 1)
			.VAlign(VAlign_Center)
			[
				SNew(SRichTextBlock)
				.DecoratorStyleSet(FConcertFrontendStyle::Get().Get())
				.Text(GetSummary(ItemPin->ActivitySummary))
				.HighlightText(HighlightText)
			]

			+SHorizontalBox::Slot()
			.Padding(1, 1)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ForegroundColor(FLinearColor::White)
				.ButtonStyle(FEditorStyle::Get(), TEXT("FlatButton.Success"))
				.Visibility(RecoverButtonVisibility)
				.ToolTipText(LOCTEXT("RecoverThrough", "Recover through this activity"))
				.OnClicked(this, &SDisasterRecoveryActivityRow::OnRecoverClicked)
				.ContentPadding(FMargin(20, 0))
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FEditorFontGlyphs::Arrow_Circle_O_Right)
				]
			];
	}
}

FText SDisasterRecoveryActivityRow::FormatEventDateTime() const
{
	if (TSharedPtr<FConcertClientSessionActivity> ItemPin = Item.Pin())
	{
		return DisplayRelativeTime.Get() ? ConcertFrontendUtils::FormatRelativeTime(ItemPin->Activity.EventTime) : AbsoluteDateTime;
	}
	return FText::GetEmpty();
}

FReply SDisasterRecoveryActivityRow::OnRecoverClicked()
{
	if (TSharedPtr<FConcertClientSessionActivity> ItemPin = Item.Pin())
	{
		OnRecoverFunc(ItemPin);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
