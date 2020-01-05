// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertSessionRecovery.h"

#include "IConcertClientWorkspace.h"
#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "SConcertSessionActivities.h"

#define LOCTEXT_NAMESPACE "SConcertSessionRecovery"

void SConcertSessionRecovery::Construct(const FArguments& InArgs)
{
	IntroductionText = InArgs._IntroductionText;
	ParentWindow = InArgs._ParentWindow;
	OnRestoreFn = InArgs._OnRestore;

	ActivityViewOptions = MakeShared<FConcertSessionActivitiesOptions>();
	ActivityViewOptions->bEnableConnectionActivityFiltering = InArgs._IsConnectionActivityFilteringEnabled;
	ActivityViewOptions->bEnableLockActivityFiltering = InArgs._IsLockActivityFilteringEnabled;

	SAssignNew(ActivityView, SConcertSessionActivities)
	.OnFetchActivities(InArgs._OnFetchActivities)
	.OnMapActivityToClient(InArgs._OnMapActivityToClient)
	.OnMakeColumnOverlayWidget([this](TWeakPtr<FConcertClientSessionActivity> Activity, const FName& ColumnId) { return MakeRecoverThroughWidget(Activity, ColumnId); })
	.HighlightText(this, &SConcertSessionRecovery::HighlightSearchText)
	.TimeFormat(ActivityViewOptions.Get(), &FConcertSessionActivitiesOptions::GetTimeFormat)
	.ClientAvatarColorColumnVisibility(InArgs._ClientAvatarColorColumnVisibility)
	.ClientNameColumnVisibility(InArgs._ClientNameColumnVisibility)
	.OperationColumnVisibility(InArgs._OperationColumnVisibility)
	.PackageColumnVisibility(InArgs._PackageColumnVisibility)
	.ConnectionActivitiesVisibility(ActivityViewOptions.Get(), &FConcertSessionActivitiesOptions::GetConnectionActivitiesVisibility)
	.LockActivitiesVisibility(ActivityViewOptions.Get(), &FConcertSessionActivitiesOptions::GetLockActivitiesVisibility)
	.DetailsAreaVisibility(InArgs._DetailsAreaVisibility);

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

					// Contextual introduction.
					+SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0, 6)
					[
						SNew(STextBlock)
						.Text(IntroductionText)
						.Visibility(IntroductionText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]

					// Search bar.
					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 1, 0, 2)
					[
						SAssignNew(SearchBox, SSearchBox)
						.HintText(LOCTEXT("SearchHint", "Search..."))
						.OnTextChanged(this, &SConcertSessionRecovery::OnSearchTextChanged)
						.OnTextCommitted(this, &SConcertSessionRecovery::OnSearchTextCommitted)
						.DelayChangeNotificationsWhileTyping(true)
					]

					// Activity List
					+SVerticalBox::Slot()
					[
						ActivityView.ToSharedRef()
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
						ActivityViewOptions->MakeStatusBar(
							TAttribute<int32>(ActivityView.Get(), &SConcertSessionActivities::GetTotalActivityNum),
							TAttribute<int32>(ActivityView.Get(), &SConcertSessionActivities::GetDisplayedActivityNum))
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
					.ToolTipText(LOCTEXT("RecoverTooltip", "Replay all recorded transactions through the most recent one, including the ones currently filtered out by the view."))
					.OnClicked(this, &SConcertSessionRecovery::OnRecoverAllClicked)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(14, 3))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RecoverAll", "Recover All"))
						.Font( FEditorStyle::GetFontStyle("BoldFont"))
						.ShadowOffset( FVector2D( 1.0f, 1.0f ) )
					]
				]

				+SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.ForegroundColor(FLinearColor::White)
					.ButtonStyle(FEditorStyle::Get(), TEXT("FlatButton.Danger"))
					.ToolTipText(LOCTEXT("CancelRecoveryTooltip", "Discard any recoverable data for your assets and continue with their last saved state"))
					.OnClicked(this, &SConcertSessionRecovery::OnCancelRecoveryClicked)
					.HAlign(HAlign_Center)
					.ContentPadding(FMargin(14, 3))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Cancel", "Cancel"))
						.Font( FEditorStyle::GetFontStyle("BoldFont"))
						.ShadowOffset( FVector2D( 1.0f, 1.0f ) )
					]
				]
			]
		]
	];
}

TSharedPtr<SWidget> SConcertSessionRecovery::MakeRecoverThroughWidget(TWeakPtr<FConcertClientSessionActivity> Activity, const FName& ColumnId)
{
	if (ActivityView->IsLastColumn(ColumnId)) // The most right cell.
	{
		// The green 'Recover Through' button that appears in the most right cell if the row is selected.
		return SNew(SBox)
		.Padding(FMargin(1, 1))
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ForegroundColor(FLinearColor::White)
			.ButtonStyle(FEditorStyle::Get(), TEXT("FlatButton.Success"))
			.Visibility_Lambda([this, Activity](){ return GetRecoverThroughButtonVisibility(Activity.Pin()); })
			.OnClicked_Lambda([this, Activity](){ RecoverThrough(Activity.Pin()); return FReply::Handled(); })
			.ToolTipText(LOCTEXT("RecoverThrough", "Replay all prior transactions through this activity, including the ones currently filtered out by the view."))
			.ContentPadding(FMargin(20, 0))
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FEditorFontGlyphs::Arrow_Circle_O_Right)
			]
		];
	}

	return nullptr; // No overlay for other columns.
}

EVisibility SConcertSessionRecovery::GetRecoverThroughButtonVisibility(TSharedPtr<FConcertClientSessionActivity> Activity)
{
	return Activity == ActivityView->GetSelectedActivity() ? EVisibility::Visible : EVisibility::Hidden;
}

void SConcertSessionRecovery::OnSearchTextChanged(const FText& InFilterText)
{
	SearchText = InFilterText;
	SearchBox->SetError(ActivityView->UpdateTextFilter(InFilterText));
}

void SConcertSessionRecovery::OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType)
{
	if (!InFilterText.EqualTo(SearchText))
	{
		OnSearchTextChanged(InFilterText);
	}
}

FText SConcertSessionRecovery::HighlightSearchText() const
{
	return SearchText;
}

FReply SConcertSessionRecovery::OnCancelRecoveryClicked()
{
	check(!RecoveryThroughItem.IsValid());

	DismissWindow();
	return FReply::Handled();
}

FReply SConcertSessionRecovery::OnRecoverAllClicked()
{
	// Recover to the most recent activity, ignoring any filter being applied to the view.
	RecoverThrough(ActivityView->GetMostRecentActivity());
	return FReply::Handled();
}

void SConcertSessionRecovery::RecoverThrough(TSharedPtr<FConcertClientSessionActivity> Item)
{
	bool bShouldDismissWindow = true;
	if (Item)
	{
		RecoveryThroughItem = Item;

		if (OnRestoreFn)
		{
			bShouldDismissWindow = OnRestoreFn(RecoveryThroughItem);
		}
	}

	if (bShouldDismissWindow)
	{
		DismissWindow();
	}
}

void SConcertSessionRecovery::DismissWindow()
{
	if (TSharedPtr<SWindow> ParentWindowPin = ParentWindow.Pin())
	{
		ParentWindowPin->RequestDestroyWindow();
	}
}

#undef LOCTEXT_NAMESPACE
