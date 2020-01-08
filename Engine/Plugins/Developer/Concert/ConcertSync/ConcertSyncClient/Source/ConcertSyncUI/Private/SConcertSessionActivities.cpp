// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SConcertSessionActivities.h"

#include "IConcertClientWorkspace.h"
#include "EditorStyleSet.h"
#include "Algo/Transform.h"
#include "Misc/AsyncTaskNotification.h"
#include "Misc/ITransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SUndoHistoryDetails.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "ConcertFrontendStyle.h"
#include "ConcertFrontendUtils.h"
#include "ConcertLogGlobal.h"
#include "ConcertTransactionEvents.h"
#include "ConcertWorkspaceData.h"
#include "SPackageDetails.h"

#define LOCTEXT_NAMESPACE "SConcertSessionActivities"

namespace ConcertSessionActivityUtils
{
// The columns names.
const FName DateTimeColumnId    = TEXT("DateTime");
const FName OperationColumnId   = TEXT("Operation");
const FName PackageColumnId     = TEXT("Package");
const FName SummaryColumnId     = TEXT("Summary");
const FName ClientNameColumnId  = TEXT("Client");
const FName AvatarColorColumnId = TEXT("Client AvatarColor");

// The View Options check boxes.
const FName DisplayRelativeTimeCheckBoxId       = TEXT("DisplayRelativeTime");
const FName ShowConnectionActivitiesCheckBoxId  = TEXT("ShowConnectionActivities");
const FName ShowLockActivitiesCheckBoxId        = TEXT("ShowLockActivities");
const FName ShowPackageActivitiesCheckBoxId     = TEXT("ShowPackageActivities");
const FName ShowTransactionActivitiesCheckBoxId = TEXT("ShowTransactionActivities");
const FName ShowIgnoredActivitiesCheckBoxId     = TEXT("ShowIgnoredActivities");

FText GetActivityDateTime(const FConcertClientSessionActivity& Activity, SConcertSessionActivities::ETimeFormat TimeFormat)
{
	return TimeFormat == SConcertSessionActivities::ETimeFormat::Relative ? ConcertFrontendUtils::FormatRelativeTime(Activity.Activity.EventTime) : FText::AsDateTime(Activity.Activity.EventTime);
}

FText GetOperationName(const FConcertClientSessionActivity& Activity)
{
	if (const FConcertSyncTransactionActivitySummary* TransactionSummary = Activity.ActivitySummary.Cast<FConcertSyncTransactionActivitySummary>())
	{
		return TransactionSummary->TransactionTitle;
	}

	if (const FConcertSyncPackageActivitySummary* PackageSummary = Activity.ActivitySummary.Cast<FConcertSyncPackageActivitySummary>())
	{
		switch (PackageSummary->PackageUpdateType)
		{
		case EConcertPackageUpdateType::Added   : return LOCTEXT("NewPackageOperation",    "New Package");
		case EConcertPackageUpdateType::Deleted : return LOCTEXT("DeletePackageOperation", "Delete Package");
		case EConcertPackageUpdateType::Renamed : return LOCTEXT("RenamePackageOperation", "Rename Package");
		case EConcertPackageUpdateType::Saved   : return LOCTEXT("SavePackageOperation",   "Save Package");
		case EConcertPackageUpdateType::Dummy: // Fall-Through
		default: break;
		}
	}

	if (const FConcertSyncConnectionActivitySummary* ConnectionSummary = Activity.ActivitySummary.Cast<FConcertSyncConnectionActivitySummary>())
	{
		switch (ConnectionSummary->ConnectionEventType)
		{
		case EConcertSyncConnectionEventType::Connected:    return LOCTEXT("JoinOperation", "Join Session");
		case EConcertSyncConnectionEventType::Disconnected: return LOCTEXT("LeaveOperation", "Leave Session");
		default: break;
		}
	}

	if (const FConcertSyncLockActivitySummary* LockSummary = Activity.ActivitySummary.Cast<FConcertSyncLockActivitySummary>())
	{
		switch (LockSummary->LockEventType)
		{
		case EConcertSyncLockEventType::Locked:   return LOCTEXT("LockOperation", "Lock");
		case EConcertSyncLockEventType::Unlocked: return LOCTEXT("UnlockOperation", "Unlock");
		default: break;
		}
	}

	return FText::GetEmpty();
}

FText GetPackageName(const FConcertClientSessionActivity& Activity)
{
	if (const FConcertSyncPackageActivitySummary* Summary = Activity.ActivitySummary.Cast<FConcertSyncPackageActivitySummary>())
	{
		return FText::FromName(Summary->PackageName);
	}

	if (const FConcertSyncTransactionActivitySummary* Summary = Activity.ActivitySummary.Cast<FConcertSyncTransactionActivitySummary>())
	{
		return FText::FromName(Summary->PrimaryPackageName);
	}

	return FText::GetEmpty();
}

FText GetSummary(const FConcertClientSessionActivity& Activity, const FText& ClientName, bool bAsRichText)
{
	if (const FConcertSyncActivitySummary* Summary = Activity.ActivitySummary.Cast<FConcertSyncActivitySummary>())
	{
		return Summary->ToDisplayText(ClientName, bAsRichText);
	}

	return FText::GetEmpty();
}

FText GetClientName(const FConcertClientInfo* InActivityClient)
{
	return InActivityClient ? FText::AsCultureInvariant(InActivityClient->DisplayName) : FText::GetEmpty();
}

};

/**
 * Displays the summary of an activity recorded and recoverable in the SConcertSessionRecovery list view.
 */
class SConcertSessionActivityRow : public SMultiColumnTableRow<TSharedPtr<FConcertClientSessionActivity>>
{
public:
	SLATE_BEGIN_ARGS(SConcertSessionActivityRow)
		: _TimeFormat(SConcertSessionActivities::ETimeFormat::Relative)
		, _HighlightText()
		, _OnMakeColumnOverlayWidget()
	{
	}

	SLATE_ATTRIBUTE(SConcertSessionActivities::ETimeFormat, TimeFormat)
	SLATE_ATTRIBUTE(FText, HighlightText)
	SLATE_ARGUMENT(SConcertSessionActivities::FMakeColumnOverlayWidgetFunc, OnMakeColumnOverlayWidget) // Function invoked when generating a row to add a widget above the column widget.

	SLATE_END_ARGS()

public:
	/**
	 * Constructs a row widget to display a Concert activity.
	 * @param InArgs The widgets arguments.
	 * @param InActivty The activity to display.
	 * @param InActivityClient The client who produced this activity. Can be null if unknown or not desirable.
	 * @param InOwnerTableView The table view that will own this row.
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FConcertClientSessionActivity> InActivity, const FConcertClientInfo* InActivityClient, const TSharedRef<STableViewBase>& InOwnerTableView);

	/** Generates the widget representing this row. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

private:
	/** Format the the time displayed as relative or absolute according to the attribute 'TimeFormat'. */
	FText FormatEventDateTime() const;

	/** Returns the client avatar color. */
	FLinearColor GetClientAvatarColor() const { return ClientAvatarColor; }

	/** Returns the display name of the client who performed the activity or an empty text if the information wasn't available or desired. */
	FText GetClientName() const { return ClientName; }

	/** Generates the tooltip for this row. */
	FText MakeTooltipText() const;

private:
	TWeakPtr<FConcertClientSessionActivity> Activity;
	TAttribute<SConcertSessionActivities::ETimeFormat> TimeFormat;
	FText AbsoluteDateTime;
	FText ClientName;
	FLinearColor ClientAvatarColor;
	TAttribute<FText> HighlightText;
	SConcertSessionActivities::FMakeColumnOverlayWidgetFunc OnMakeColumnOverlayWidget;
};


void SConcertSessionActivityRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertClientSessionActivity> InActivity, const FConcertClientInfo* InActivityClient, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Activity = InActivity;
	TimeFormat = InArgs._TimeFormat;
	HighlightText = InArgs._HighlightText;
	OnMakeColumnOverlayWidget = InArgs._OnMakeColumnOverlayWidget;
	AbsoluteDateTime = ConcertSessionActivityUtils::GetActivityDateTime(*InActivity, SConcertSessionActivities::ETimeFormat::Absolute); // Cache the absolute time. It doesn't changes.
	ClientName = ConcertSessionActivityUtils::GetClientName(InActivityClient);
	ClientAvatarColor = InActivityClient ? InActivityClient->AvatarColor : FConcertFrontendStyle::Get()->GetColor("Concert.DisconnectedColor");

	// Construct base class
	SMultiColumnTableRow<TSharedPtr<FConcertClientSessionActivity>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);

	if (InActivity->Activity.bIgnored)
	{
		SetColorAndOpacity(FLinearColor(0.5, 0.5, 0.5, 0.5));
	}
}

TSharedRef<SWidget> SConcertSessionActivityRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	TSharedPtr<FConcertClientSessionActivity> ActivityPin = Activity.Pin();
	TSharedRef<SOverlay> Overlay = SNew(SOverlay);

	if (ColumnId == ConcertSessionActivityUtils::AvatarColorColumnId)
	{
		Overlay->AddSlot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(2, 1)
		[
			SNew(SColorBlock)
			.Color(GetClientAvatarColor())
			.Size(FVector2D(4.f, 16.f))
		];
	}
	else if (ColumnId == ConcertSessionActivityUtils::DateTimeColumnId)
	{
		Overlay->AddSlot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SConcertSessionActivityRow::FormatEventDateTime)
			.HighlightText(HighlightText)
		];
	}
	else if (ColumnId == ConcertSessionActivityUtils::ClientNameColumnId)
	{
		Overlay->AddSlot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(GetClientName())
			.HighlightText(HighlightText)
		];
	}
	else if (ColumnId == ConcertSessionActivityUtils::PackageColumnId)
	{
		Overlay->AddSlot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(ConcertSessionActivityUtils::GetPackageName(*ActivityPin))
			.HighlightText(HighlightText)
		];
	}
	else if (ColumnId == ConcertSessionActivityUtils::OperationColumnId)
	{
		Overlay->AddSlot()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(ConcertSessionActivityUtils::GetOperationName(*ActivityPin))
			.HighlightText(HighlightText)
		];
	}
	else
	{
		check(ColumnId == ConcertSessionActivityUtils::SummaryColumnId);

		Overlay->AddSlot()
		.VAlign(VAlign_Center)
		[
			SNew(SRichTextBlock)
			.DecoratorStyleSet(FConcertFrontendStyle::Get().Get())
			.Text(ConcertSessionActivityUtils::GetSummary(*ActivityPin, FText::GetEmpty(), true/*bAsRichText*/))
			.HighlightText(HighlightText)
		];
	}

	if (OnMakeColumnOverlayWidget)
	{
		if (TSharedPtr<SWidget> OverlayedWidget = OnMakeColumnOverlayWidget(ActivityPin, ColumnId))
		{
			Overlay->AddSlot()
			[
				OverlayedWidget.ToSharedRef()
			];
		}
	}

	SetToolTipText(TAttribute<FText>(this, &SConcertSessionActivityRow::MakeTooltipText));

	return Overlay;
}

FText SConcertSessionActivityRow::MakeTooltipText() const
{
	if (TSharedPtr<FConcertClientSessionActivity> ActivityPin = Activity.Pin())
	{
		FText Client = GetClientName();
		FText Operation = ConcertSessionActivityUtils::GetOperationName(*ActivityPin);
		FText Package = ConcertSessionActivityUtils::GetPackageName(*ActivityPin);
		FText Summary = ConcertSessionActivityUtils::GetSummary(*ActivityPin, Client, /*bAsRichText*/false);

		FTextBuilder TextBuilder;

		if (!Operation.IsEmpty())
		{
			TextBuilder.AppendLine(Operation);
		}

		TextBuilder.AppendLineFormat(LOCTEXT("ActivityRowTooltip_DateTime", "{0} ({1})"), AbsoluteDateTime, ConcertFrontendUtils::FormatRelativeTime(ActivityPin->Activity.EventTime));

		if (!Package.IsEmpty())
		{
			TextBuilder.AppendLine(Package);
		}

		if (!Summary.IsEmpty())
		{
			TextBuilder.AppendLine(Summary);
		}

		if (ActivityPin->Activity.bIgnored)
		{
			TextBuilder.AppendLine();
			TextBuilder.AppendLine(LOCTEXT("IgnoredActivity", "** This activity cannot be recovered (likely recorded during a Multi-User session). It is displayed for crash inspection only. It will be ignored on restore."));
		}

		return TextBuilder.ToText();
	}

	return FText::GetEmpty();
}

FText SConcertSessionActivityRow::FormatEventDateTime() const
{
	if (TSharedPtr<FConcertClientSessionActivity> ItemPin = Activity.Pin())
	{
		return TimeFormat.Get() == SConcertSessionActivities::ETimeFormat::Relative ? ConcertFrontendUtils::FormatRelativeTime(ItemPin->Activity.EventTime) : AbsoluteDateTime;
	}
	return FText::GetEmpty();
}


void SConcertSessionActivities::Construct(const FArguments& InArgs)
{
	FetchActivitiesFn = InArgs._OnFetchActivities;
	GetActivityUserFn = InArgs._OnMapActivityToClient;
	GetTransactionEventFn = InArgs._OnGetTransactionEvent;
	GetPackageEventFn = InArgs._OnGetPackageEvent;
	MakeColumnOverlayWidgetFn = InArgs._OnMakeColumnOverlayWidget;
	HighlightText = InArgs._HighlightText;
	TimeFormat = InArgs._TimeFormat;
	ClientNameColumnVisibility = InArgs._ClientNameColumnVisibility;
	OperationColumnVisibility = InArgs._OperationColumnVisibility;
	PackageColumnVisibility = InArgs._PackageColumnVisibility;
	ConnectionActivitiesVisibility = InArgs._ConnectionActivitiesVisibility;
	LockActivitiesVisibility = InArgs._LockActivitiesVisibility;
	PackageActivitiesVisibility = InArgs._PackageActivitiesVisibility;
	TransactionActivitiesVisibility = InArgs._TransactionActivitiesVisibility;
	IgnoredActivitiesVisibility = InArgs._IgnoredActivitiesVisibility;
	DetailsAreaVisibility = InArgs._DetailsAreaVisibility;
	bAutoScrollDesired = InArgs._IsAutoScrollEnabled;

	SearchTextFilter = MakeShared<TTextFilter<const FConcertClientSessionActivity&>>(TTextFilter<const FConcertClientSessionActivity&>::FItemToStringArray::CreateSP(this, &SConcertSessionActivities::PopulateSearchStrings));
	SearchTextFilter->OnChanged().AddSP(this, &SConcertSessionActivities::OnActivityFilterUpdated);

	// Set the initial filter state.
	ActiveFilterFlags = QueryActiveActivityFilters();

	// Create the table header. (Setting visibility on the column itself doesn't show/hide the column as one would expect, unfortunately)
	TSharedRef<SHeaderRow> HeaderRow = SNew(SHeaderRow);
	if (InArgs._ClientAvatarColorColumnVisibility.Get() == EVisibility::Visible)
	{
		HeaderRow->AddColumn(SHeaderRow::Column(ConcertSessionActivityUtils::AvatarColorColumnId)
			.DefaultLabel(INVTEXT(""))
			.ManualWidth(8));
	}

	HeaderRow->AddColumn(SHeaderRow::Column(ConcertSessionActivityUtils::DateTimeColumnId)
		.DefaultLabel(LOCTEXT("DateTime", "Date/Time"))
		.ManualWidth(160));

	if (InArgs._ClientNameColumnVisibility.Get() == EVisibility::Visible)
	{
		HeaderRow->AddColumn(SHeaderRow::Column(ConcertSessionActivityUtils::ClientNameColumnId)
			.DefaultLabel(LOCTEXT("Client", "Client"))
			.ManualWidth(80));
	}

	if (InArgs._OperationColumnVisibility.Get() == EVisibility::Visible)
	{
		HeaderRow->AddColumn(SHeaderRow::Column(ConcertSessionActivityUtils::OperationColumnId)
			.DefaultLabel(LOCTEXT("Operation", "Operation"))
			.ManualWidth(160));
	}

	if (InArgs._PackageColumnVisibility.Get() == EVisibility::Visible)
	{
		HeaderRow->AddColumn(SHeaderRow::Column(ConcertSessionActivityUtils::PackageColumnId)
			.DefaultLabel(LOCTEXT("Package", "Package"))
			.ManualWidth(200));
	}

	HeaderRow->AddColumn(SHeaderRow::Column(ConcertSessionActivityUtils::SummaryColumnId)
		.DefaultLabel(LOCTEXT("Summary", "Summary")));

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Vertical)

		// Activity List
		+SSplitter::Slot()
		.Value(0.75)
		[
			SNew(SOverlay)

			+SOverlay::Slot() // Activity list itself.
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FSlateColor(FLinearColor(0.6, 0.6, 0.6)))
				.Padding(0)
				[
					SAssignNew(ActivityView, SListView<TSharedPtr<FConcertClientSessionActivity>>)
					.ListItemsSource(&Activities)
					.OnGenerateRow(this, &SConcertSessionActivities::OnGenerateActivityRowWidget)
					.SelectionMode(ESelectionMode::Single)
					.AllowOverscroll(EAllowOverscroll::No)
					.OnListViewScrolled(this, &SConcertSessionActivities::OnListViewScrolled)
					.OnSelectionChanged(this, &SConcertSessionActivities::OnListViewSelectionChanged)
					.HeaderRow(HeaderRow)
				]
			]

			+SOverlay::Slot() // Display a reason why no activities are shown.
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Visibility_Lambda([TextAttr = InArgs._NoActivitiesReasonText](){ return TextAttr.Get().IsEmptyOrWhitespace() ? EVisibility::Collapsed : EVisibility::Visible; })
				.Text(InArgs._NoActivitiesReasonText)
				.Justification(ETextJustify::Center)
			]
		]

		// Activity details.
		+SSplitter::Slot()
		.Value(0.25)
		.SizeRule(TAttribute<SSplitter::ESizeRule>(this, &SConcertSessionActivities::GetDetailsAreaSizeRule))
		[
			SAssignNew(ExpandableDetails, SExpandableArea)
			.Visibility(GetDetailAreaVisibility())
			.InitiallyCollapsed(true)
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
			.BorderImage_Lambda([this]() { return ConcertFrontendUtils::GetExpandableAreaBorderImage(*ExpandableDetails); })
			.BodyBorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.BodyBorderBackgroundColor(FLinearColor::White)
			.OnAreaExpansionChanged(this, &SConcertSessionActivities::OnDetailsAreaExpansionChanged)
			.Padding(0.0f)
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Details", "Details"))
				.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.ShadowOffset(FVector2D(1.0f, 1.0f))
			]
			.BodyContent()
			[
				SNew(SOverlay)

				+SOverlay::Slot()
				[
					SNew(SScrollBox)
					.ScrollBarThickness(FVector2D(12.0f, 5.0f)) // To have same thickness than the ListView scroll bar.

					+SScrollBox::Slot()
					[
						SAssignNew(TransactionDetailsPanel, SUndoHistoryDetails)
						.Visibility(EVisibility::Collapsed)
					]

					+SScrollBox::Slot()
					[
						SAssignNew(PackageDetailsPanel, SPackageDetails)
						.Visibility(EVisibility::Collapsed)
					]
				]

				+SOverlay::Slot()
				[
					SAssignNew(NoDetailsPanel, SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Visibility(EVisibility::Visible)
					[
						SNew(STextBlock)
						.Text(this, &SConcertSessionActivities::GetNoDetailsText)
					]
				]

				+SOverlay::Slot()
				[
					SAssignNew(LoadingDetailsPanel, SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Visibility(EVisibility::Collapsed)
					[
						SNew(SThrobber)
					]
				]
			]
		]
	];

	// Check if some activities are already available.
	FetchActivities();

	if (bAutoScrollDesired)
	{
		FSlateApplication::Get().OnPostTick().AddSP(this, &SConcertSessionActivities::OnPostTick);
	}
}

void SConcertSessionActivities::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	EConcertActivityFilterFlags LatestFilterFlags = QueryActiveActivityFilters();
	if (ActiveFilterFlags != LatestFilterFlags)
	{
		ActiveFilterFlags = LatestFilterFlags;
		OnActivityFilterUpdated();
	}

	FetchActivities(); // Check if we should fetch more activities in case we filtered out to many of them.
}

FText SConcertSessionActivities::GetNoDetailsText() const
{
	return GetSelectedActivity() ?
		LOCTEXT("NoDetails_NotAvailable", "The selected activity doesn't have details to display.") :
		LOCTEXT("NoDetails_NoActivitySelected", "Select an activity to view its details.");
}

void SConcertSessionActivities::OnPostTick(float)
{
	// NOTE: The way the list view adjust the scroll position when the component is resized has some behaviors to consider to get auto-scrolling
	//       working consistently. When the list view shrink (allowing less item) the scroll view doesn't remain anchored at the end. Instead
	//       the scroll position moves a little bit up and the list view doesn't consider it as scrolling because OnListViewScrolled() is not
	//       called. The code below detects that case and maintain the scroll position at the end when required.

	if (bActivityViewScrolled) // OnListViewScrolled() was invoked. The user scrolled the activity list or enlarged the view to see more items.
	{
		bUserScrolling = !ActivityView->GetScrollDistanceRemaining().IsNearlyZero();
		bActivityViewScrolled = false;
	}
	else if (bAutoScrollDesired && !bUserScrolling && ActivityView->GetScrollDistanceRemaining().Y > 0) // See NOTE.
	{
		ActivityView->ScrollToBottom(); // Ensure the scroll position is maintained at the end.
	}
}

void SConcertSessionActivities::OnListViewScrolled(double InScrollOffset)
{
	bActivityViewScrolled = true;

	if (FetchActivitiesFn) // This widget is responsible to populate the view.
	{
		if (!bAllActivitiesFetched && ActivityView->GetScrollDistance().Y > 0.7) // Should fetch more?
		{
			DesiredActivitiesCount += ActivitiesPerRequest; // This will request another 'page' the next time FetchActivities() is called.
		}
	}
}

void SConcertSessionActivities::OnListViewSelectionChanged(TSharedPtr<FConcertClientSessionActivity> InActivity, ESelectInfo::Type SelectInfo)
{
	UpdateDetailArea(InActivity);
}

void SConcertSessionActivities::OnDetailsAreaExpansionChanged(bool bExpanded)
{
	bDetailsAreaExpanded = bExpanded;
	UpdateDetailArea(bDetailsAreaExpanded ? GetSelectedActivity() : nullptr);
}

void SConcertSessionActivities::UpdateDetailArea(TSharedPtr<FConcertClientSessionActivity> InSelectedActivity)
{
	if (DetailsAreaVisibility != EVisibility::Visible || !bDetailsAreaExpanded)
	{
		return;
	}
	else if (!InSelectedActivity.IsValid()) // The selection was cleared?
	{
		SetDetailsPanelVisibility(NoDetailsPanel.Get());
	}
	else if (InSelectedActivity->EventPayload) // The event payload is already bundled in the activity stream?
	{
		if (InSelectedActivity->Activity.EventType == EConcertSyncActivityEventType::Transaction)
		{
			FConcertSyncTransactionEvent TransactionEvent;
			InSelectedActivity->EventPayload->GetTypedPayload(TransactionEvent);
			if (TransactionEvent.Transaction.ExportedObjects.Num())
			{
				DisplayTransactionDetails(*InSelectedActivity, TransactionEvent.Transaction);
			}
			else
			{
				SetDetailsPanelVisibility(NoDetailsPanel.Get());
			}
		}
		else if (InSelectedActivity->Activity.EventType == EConcertSyncActivityEventType::Package)
		{
			FConcertSyncPackageEvent PackageEvent;
			InSelectedActivity->EventPayload->GetTypedPayload(PackageEvent);
			DisplayPackageDetails(*InSelectedActivity, PackageEvent);
		}
		else // Other activity types (lock/connection) don't have details panel.
		{
			SetDetailsPanelVisibility(NoDetailsPanel.Get());
		}
	}
	else if (InSelectedActivity->Activity.EventType == EConcertSyncActivityEventType::Transaction && GetTransactionEventFn) // A function is bound to get the transaction event?
	{
		SetDetailsPanelVisibility(LoadingDetailsPanel.Get());
		TWeakPtr<SConcertSessionActivities> WeakSelf = SharedThis(this);
		GetTransactionEventFn(*InSelectedActivity).Next([WeakSelf, InSelectedActivity](const TOptional<FConcertSyncTransactionEvent>& TransactionEvent)
		{
			if (TSharedPtr<SConcertSessionActivities> Self = WeakSelf.Pin()) // If 'this' object hasn't been deleted.
			{
				if (Self->GetSelectedActivity() == InSelectedActivity) // Ensure the activity is still selected.
				{
					if (TransactionEvent.IsSet() && TransactionEvent->Transaction.ExportedObjects.Num())
					{
						Self->DisplayTransactionDetails(*InSelectedActivity, TransactionEvent.GetValue().Transaction);
					}
					else
					{
						Self->SetDetailsPanelVisibility(Self->NoDetailsPanel.Get());
					}
				}
				// else -> The details panel is presenting information for another activity (or no activity).
			}
			// else -> The widget was deleted.
		});
	}
	else if (InSelectedActivity->Activity.EventType == EConcertSyncActivityEventType::Package && GetPackageEventFn) // A function is bound to get the package event?
	{
		SetDetailsPanelVisibility(LoadingDetailsPanel.Get());
		TWeakPtr<SConcertSessionActivities> WeakSelf = SharedThis(this);
		GetPackageEventFn(*InSelectedActivity).Next([WeakSelf, InSelectedActivity](const TOptional<FConcertSyncPackageEvent>& PackageEvent)
		{
			if (TSharedPtr<SConcertSessionActivities> Self = WeakSelf.Pin()) // If 'this' object hasn't been deleted.
			{
				if (Self->GetSelectedActivity() == InSelectedActivity) // Ensure the activity is still selected.
				{
					if (PackageEvent.IsSet())
					{
						Self->DisplayPackageDetails(*InSelectedActivity, PackageEvent.GetValue());
					}
					else
					{
						Self->SetDetailsPanelVisibility(Self->NoDetailsPanel.Get());
					}
				}
				// else -> The details panel is presenting information for another activity (or no activity).
			}
			// else -> The widget was deleted.
		});
	}
	else
	{
		SetDetailsPanelVisibility(NoDetailsPanel.Get());
	}
}

EConcertActivityFilterFlags SConcertSessionActivities::QueryActiveActivityFilters() const
{
	// The visibility attributes are externally provided. (In practice, they are controlled from the 'View Options' check boxes).
	EConcertActivityFilterFlags ActiveFlags = EConcertActivityFilterFlags::ShowAll;

	if (ConnectionActivitiesVisibility.Get() != EVisibility::Visible)
	{
		ActiveFlags |= EConcertActivityFilterFlags::HideConnectionActivities;
	}
	if (LockActivitiesVisibility.Get() != EVisibility::Visible)
	{
		ActiveFlags |= EConcertActivityFilterFlags::HideLockActivities;
	}
	if (PackageActivitiesVisibility.Get() != EVisibility::Visible)
	{
		ActiveFlags |= EConcertActivityFilterFlags::HidePackageActivities;
	}
	if (TransactionActivitiesVisibility.Get() != EVisibility::Visible)
	{
		ActiveFlags |= EConcertActivityFilterFlags::HideTransactionActivities;
	}
	if (IgnoredActivitiesVisibility.Get() != EVisibility::Visible)
	{
		ActiveFlags |= EConcertActivityFilterFlags::HideIgnoredActivities;
	}

	return ActiveFlags;
}

void SConcertSessionActivities::OnActivityFilterUpdated()
{
	// Try preserving the selected activity.
	TSharedPtr<FConcertClientSessionActivity> SelectedActivity = GetSelectedActivity();

	// Reset the list of displayed activities.
	Activities.Reset(AllActivities.Num());

	// Apply the filter.
	for (TSharedPtr<FConcertClientSessionActivity>& Activity : AllActivities)
	{
		if (PassesFilters(*Activity))
		{
			Activities.Add(Activity);
		}
	}

	// Restore/reset the selected activity.
	if (SelectedActivity && Activities.Contains(SelectedActivity))
	{
		ActivityView->SetItemSelection(SelectedActivity, true); // Restore previous selection.
		ActivityView->RequestScrollIntoView(SelectedActivity);
	}
	else if (bAutoScrollDesired && !bUserScrolling) // No activity was selected.
	{
		ActivityView->ScrollToBottom();
	}

	ActivityView->RequestListRefresh();
}

void SConcertSessionActivities::FetchActivities()
{
	if (!FetchActivitiesFn) // Not bound?
	{
		return; // The widget is expected to be populated/cleared externally using Append()/Reset()
	}

	bool bRefresh = false;

	// If they are still activities to fetch and the user scrolled down (or our nominal amount is not reached), request more from the server.
	if (!bAllActivitiesFetched && (Activities.Num() < DesiredActivitiesCount))
	{
		FText ErrorMsg;
		int32 FetchCount = 0; // The number of activities fetched in this iteration.
		int32 StartInsertPos = AllActivities.Num();

		bAllActivitiesFetched = FetchActivitiesFn(AllActivities, FetchCount, ErrorMsg);
		if (ErrorMsg.IsEmpty())
		{
			if (FetchCount) // New activities appended?
			{
				for (int32 Index = StartInsertPos; Index < AllActivities.Num(); ++Index) // Append the fetched activities
				{
					if (PassesFilters(*AllActivities[Index]))
					{
						Activities.Add(AllActivities[Index]);
						bRefresh = true;
					}

					if (AllActivities[Index]->Activity.bIgnored)
					{
						++IgnoredActivityNum;
					}
				}
			}
		}
		else
		{
			FAsyncTaskNotificationConfig NotificationConfig;
			NotificationConfig.bIsHeadless = false;
			NotificationConfig.bKeepOpenOnFailure = true;
			NotificationConfig.LogCategory = &LogConcert;

			FAsyncTaskNotification Notification(NotificationConfig);
			Notification.SetComplete(LOCTEXT("FetchError", "Failed to retrieve session activities"), ErrorMsg, /*Success*/ false);
		}
	}

	if (Activities.Num() && ActivityView->GetSelectedItems().Num() == 0)
	{
		ActivityView->SetItemSelection(Activities[0], true);
	}

	if (bRefresh)
	{
		if (bAutoScrollDesired && !bUserScrolling)
		{
			ActivityView->ScrollToBottom();
		}

		ActivityView->RequestListRefresh();
	}
}

void SConcertSessionActivities::Append(TSharedPtr<FConcertClientSessionActivity> Activity)
{
	if (Activity->Activity.bIgnored)
	{
		++IgnoredActivityNum;
	}

	AllActivities.Add(Activity);
	if (PassesFilters(*Activity))
	{
		Activities.Add(MoveTemp(Activity));

		if (bAutoScrollDesired && !bUserScrolling)
		{
			ActivityView->ScrollToBottom();
		}

		ActivityView->RequestListRefresh();
	}
}

void SConcertSessionActivities::RequestRefresh()
{
	ActivityView->RequestListRefresh();
}

void SConcertSessionActivities::Reset()
{
	Activities.Reset();
	AllActivities.Reset();
	ActivityView->RequestListRefresh();
	bAllActivitiesFetched = false;
	bUserScrolling = false;
	DesiredActivitiesCount = ActivitiesPerRequest;
	IgnoredActivityNum = 0;
}

bool SConcertSessionActivities::PassesFilters(const FConcertClientSessionActivity& Activity)
{
	if (Activity.Activity.EventType == EConcertSyncActivityEventType::Connection && ConnectionActivitiesVisibility.Get() != EVisibility::Visible) // Filter out 'connection' activities?
	{
		return false;
	}
	else if (Activity.Activity.EventType == EConcertSyncActivityEventType::Lock && LockActivitiesVisibility.Get() != EVisibility::Visible) // Filter out 'lock' activities?
	{
		return false;
	}
	else if (Activity.Activity.EventType == EConcertSyncActivityEventType::Package && PackageActivitiesVisibility.Get() != EVisibility::Visible) // Filter out 'package' activities?
	{
		return false;
	}
	else if (Activity.Activity.EventType == EConcertSyncActivityEventType::Transaction && TransactionActivitiesVisibility.Get() != EVisibility::Visible) // Filter out 'transaction' activities?
	{
		return false;
	}
	else if (Activity.Activity.bIgnored && IgnoredActivitiesVisibility.Get() != EVisibility::Visible) // Filter out 'ignored' activities?
	{
		return false;
	}

	return SearchTextFilter->PassesFilter(Activity);
}

FText SConcertSessionActivities::UpdateTextFilter(const FText& InFilterText)
{
	SearchTextFilter->SetRawFilterText(InFilterText);
	return SearchTextFilter->GetFilterErrorText();
}

void SConcertSessionActivities::PopulateSearchStrings(const FConcertClientSessionActivity& Activity, TArray<FString>& OutSearchStrings) const
{
	FText ClientName = GetActivityUserFn ? ConcertSessionActivityUtils::GetClientName(GetActivityUserFn(Activity.Activity.EndpointId)) : FText::GetEmpty();

	OutSearchStrings.Add(ConcertSessionActivityUtils::GetActivityDateTime(Activity, TimeFormat.Get()).ToString());
	OutSearchStrings.Add(ConcertSessionActivityUtils::GetSummary(Activity, ClientName, /*bAsRichText*/false).ToString());

	if (ClientNameColumnVisibility.Get() == EVisibility::Visible)
	{
		OutSearchStrings.Add(ClientName.ToString());
	}

	if (OperationColumnVisibility.Get() == EVisibility::Visible)
	{
		OutSearchStrings.Add(ConcertSessionActivityUtils::GetOperationName(Activity).ToString());
	}

	if (PackageColumnVisibility.Get() == EVisibility::Visible)
	{
		OutSearchStrings.Add(ConcertSessionActivityUtils::GetPackageName(Activity).ToString());
	}
}

TSharedRef<ITableRow> SConcertSessionActivities::OnGenerateActivityRowWidget(TSharedPtr<FConcertClientSessionActivity> Activity, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SConcertSessionActivityRow, Activity, GetActivityUserFn ? GetActivityUserFn(Activity->Activity.EndpointId) : nullptr, OwnerTable)
		.TimeFormat(TimeFormat)
		.HighlightText(HighlightText)
		.OnMakeColumnOverlayWidget(MakeColumnOverlayWidgetFn);
}

TSharedPtr<FConcertClientSessionActivity> SConcertSessionActivities::GetSelectedActivity() const
{
	TArray<TSharedPtr<FConcertClientSessionActivity>> SelectedItems = ActivityView->GetSelectedItems();
	return SelectedItems.Num() ? SelectedItems[0] : nullptr;
}

TSharedPtr<FConcertClientSessionActivity> SConcertSessionActivities::GetMostRecentActivity() const
{
	// NOTE: This function assumes that activities are sorted by ID. When used for recovery purpose,
	//       the activities are listed from the most recent to the oldest. When displaying a live
	//       session activity stream, the activities are listed from the oldest to the newest.

	if (AllActivities.Num()) // Ignore the filters.
	{
		if (AllActivities[0]->Activity.ActivityId > AllActivities.Last()->Activity.ActivityId)
		{
			return AllActivities[0]; // Listed from the latest to oldest.
		}
		return AllActivities.Last(); // Listed from the oldest to latest.
	}
	return nullptr; // The list is empty.
}

bool SConcertSessionActivities::IsLastColumn(const FName& ColumnId) const
{
	return ColumnId == ConcertSessionActivityUtils::SummaryColumnId; // Summary column is always visible and always the last.
}

void SConcertSessionActivities::DisplayTransactionDetails(const FConcertClientSessionActivity& Activity, const FConcertTransactionEventBase& InTransaction)
{
	const FConcertSyncTransactionActivitySummary* Summary = Activity.ActivitySummary.Cast<FConcertSyncTransactionActivitySummary>();
	FString TransactionTitle = Summary ? Summary->TransactionTitle.ToString() : FString();

	FTransactionDiff TransactionDiff{ InTransaction.TransactionId, TransactionTitle };

	for (const FConcertExportedObject& ExportedObject : InTransaction.ExportedObjects)
	{
		FTransactionObjectDeltaChange DeltaChange;
		Algo::Transform(ExportedObject.PropertyDatas, DeltaChange.ChangedProperties, [](const FConcertSerializedPropertyData& PropertyData) { return PropertyData.PropertyName; });

		DeltaChange.bHasNameChange = ExportedObject.ObjectData.NewOuterPathName != FName();
		DeltaChange.bHasOuterChange = ExportedObject.ObjectData.NewOuterPathName != FName();
		DeltaChange.bHasPendingKillChange = ExportedObject.ObjectData.bIsPendingKill;

		FString ObjectPathName = ExportedObject.ObjectId.ObjectOuterPathName.ToString() + TEXT(".") + ExportedObject.ObjectId.ObjectName.ToString();
		TSharedPtr<FTransactionObjectEvent> Event = MakeShared<FTransactionObjectEvent>(InTransaction.TransactionId, InTransaction.OperationId, ETransactionObjectEventType::Finalized, MoveTemp(DeltaChange), nullptr, ExportedObject.ObjectId.ObjectName, FName(*MoveTemp(ObjectPathName)), ExportedObject.ObjectId.ObjectOuterPathName, ExportedObject.ObjectId.ObjectClassPathName);

		TransactionDiff.DiffMap.Emplace(FName(*ObjectPathName), MoveTemp(Event));
	}

	TransactionDetailsPanel->SetSelectedTransaction(MoveTemp(TransactionDiff));
	SetDetailsPanelVisibility(TransactionDetailsPanel.Get());
}

void SConcertSessionActivities::DisplayPackageDetails(const FConcertClientSessionActivity& Activity, const FConcertSyncPackageEvent& PackageEvent)
{
	const FConcertClientInfo* ClientInfo = nullptr;
	if (GetActivityUserFn)
	{
		ClientInfo = GetActivityUserFn(Activity.Activity.EndpointId);
	}

	PackageDetailsPanel->SetPackageInfo(PackageEvent.Package.Info, PackageEvent.PackageRevision, ClientInfo ? ClientInfo->DisplayName : FString());
	SetDetailsPanelVisibility(PackageDetailsPanel.Get());
}

void SConcertSessionActivities::SetDetailsPanelVisibility(const SWidget* VisiblePanel)
{
	TransactionDetailsPanel->SetVisibility(VisiblePanel == TransactionDetailsPanel.Get() ? EVisibility::Visible : EVisibility::Collapsed);
	PackageDetailsPanel->SetVisibility(VisiblePanel == PackageDetailsPanel.Get() ? EVisibility::Visible : EVisibility::Collapsed);
	NoDetailsPanel->SetVisibility(VisiblePanel == NoDetailsPanel.Get() ? EVisibility::Visible : EVisibility::Collapsed);
	LoadingDetailsPanel->SetVisibility(VisiblePanel == LoadingDetailsPanel.Get() ? EVisibility::Visible : EVisibility::Collapsed);
}

TSharedRef<SWidget> FConcertSessionActivitiesOptions::MakeMenuWidget()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("DisplayRelativeTime", "Display Relative Time"),
		LOCTEXT("DisplayRelativeTime_Tooltip", "Displays Time Relative to the Current Time"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FConcertSessionActivitiesOptions::OnOptionToggled, ConcertSessionActivityUtils::DisplayRelativeTimeCheckBoxId),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked::CreateLambda([this] { return bDisplayRelativeTime; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	if (bEnablePackageActivityFiltering)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowPackageActivities", "Show Package Activities"),
			LOCTEXT("ShowPackageActivities_Tooltip", "Displays create/save/rename/delete package events."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FConcertSessionActivitiesOptions::OnOptionToggled, ConcertSessionActivityUtils::ShowPackageActivitiesCheckBoxId),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return bDisplayPackageActivities; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}

	if (bEnableTransactionActivityFiltering)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowTransactionActivities", "Show Transaction Activities"),
			LOCTEXT("ShowTransactionActivities_Tooltip", "Displays changes performed on assets."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FConcertSessionActivitiesOptions::OnOptionToggled, ConcertSessionActivityUtils::ShowTransactionActivitiesCheckBoxId),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return bDisplayTransactionActivities; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}

	if (bEnableConnectionActivityFiltering)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowConnectionActivities", "Show Connection Activities"),
			LOCTEXT("ShowConnectionActivities_Tooltip", "Displays when client joined or left the session"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FConcertSessionActivitiesOptions::OnOptionToggled, ConcertSessionActivityUtils::ShowConnectionActivitiesCheckBoxId),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return bDisplayConnectionActivities; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}

	if (bEnableLockActivityFiltering)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowLockActivities", "Show Lock Activities"),
			LOCTEXT("ShowLockActivities_Tooltip", "Displays lock/unlock events"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FConcertSessionActivitiesOptions::OnOptionToggled, ConcertSessionActivityUtils::ShowLockActivitiesCheckBoxId),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return bDisplayLockActivities; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}

	if (bEnableIgnoredActivityFiltering)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowIgnoredActivities", "Show Unrecoverable Activities"),
			LOCTEXT("ShowIgnoredActivities_Tooltip", "Displays activities that were recorded, but could not be recovered in this context."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &FConcertSessionActivitiesOptions::OnOptionToggled, ConcertSessionActivityUtils::ShowIgnoredActivitiesCheckBoxId),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FIsActionChecked::CreateLambda([this] { return bDisplayIgnoredActivities; })),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FConcertSessionActivitiesOptions::MakeViewOptionsWidget()
{
	return SNew(SComboButton)
	.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
	.ForegroundColor(FLinearColor::White)
	.ContentPadding(0)
	.OnGetMenuContent(this, &FConcertSessionActivitiesOptions::MakeMenuWidget)
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
	];
}

TSharedRef<SWidget> FConcertSessionActivitiesOptions::MakeDisplayedActivityCountWidget(TAttribute<int32> Total, TAttribute<int32> Displayed)
{
	return SNew(STextBlock)
		.Text_Lambda([Total = MoveTemp(Total), Displayed = MoveTemp(Displayed)]()
		{
			if (Total.Get() == Displayed.Get())
			{
				return FText::Format(LOCTEXT("OperationCount", "{0} operations"), Total.Get());
			}
			else
			{
				return FText::Format(LOCTEXT("PartialOperationCount", "Showing {0} of {1} {1}|plural(one=operation,other=operations)"), Displayed.Get(), Total.Get());
			}
		});
}

TSharedRef<SWidget> FConcertSessionActivitiesOptions::MakeStatusBar(TAttribute<int32> Total, TAttribute<int32> Displayed)
{
	return SNew(SHorizontalBox)

	// Operation count.
	+SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		MakeDisplayedActivityCountWidget(MoveTemp(Total), MoveTemp(Displayed))
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
		MakeViewOptionsWidget()
	];
}

void FConcertSessionActivitiesOptions::OnOptionToggled(const FName CheckBoxId)
{
	if (CheckBoxId == ConcertSessionActivityUtils::DisplayRelativeTimeCheckBoxId)
	{
		bDisplayRelativeTime = !bDisplayRelativeTime;
	}
	else if (CheckBoxId == ConcertSessionActivityUtils::ShowConnectionActivitiesCheckBoxId)
	{
		bDisplayConnectionActivities = !bDisplayConnectionActivities;
	}
	else if (CheckBoxId == ConcertSessionActivityUtils::ShowLockActivitiesCheckBoxId)
	{
		bDisplayLockActivities = !bDisplayLockActivities;
	}
	else if (CheckBoxId == ConcertSessionActivityUtils::ShowPackageActivitiesCheckBoxId)
	{
		bDisplayPackageActivities = !bDisplayPackageActivities;
	}
	else if (CheckBoxId == ConcertSessionActivityUtils::ShowTransactionActivitiesCheckBoxId)
	{
		bDisplayTransactionActivities = !bDisplayTransactionActivities;
	}
	else if (CheckBoxId == ConcertSessionActivityUtils::ShowIgnoredActivitiesCheckBoxId)
	{
		bDisplayIgnoredActivities = !bDisplayIgnoredActivities;
	}	
}

#undef LOCTEXT_NAMESPACE
