// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Async/Future.h"
#include "Misc/TextFilter.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "ConcertSyncSessionTypes.h"

struct FConcertClientSessionActivity;
struct FConcertClientInfo;
struct FConcertPackageInfo;
struct FConcertTransactionEventBase;
class SUndoHistoryDetails;
class SPackageDetails;
class STextBlock;
class SExpendableArea;


/** Filters for the concert session activity view. */
enum class EConcertActivityFilterFlags
{
	ShowAll                  = 0x00,
	HideConnectionActivities = 1<<0,
	HideLockActivities       = 1<<1,
	HidePackageActivities    = 1<<2,
	HideTransactionActivities= 1<<3,
	HideIgnoredActivities    = 1<<4,
};
ENUM_CLASS_FLAGS(EConcertActivityFilterFlags);

/** Displays session activities in a table view. */
class CONCERTSYNCUI_API SConcertSessionActivities : public SCompoundWidget
{
public:
	/** Defines how the time should be displayed in the date/time column. */
	enum class ETimeFormat
	{
		Relative, // Display relative time (23 seconds ago)
		Absolute  // Display absolute time (April 7, 2019 - 10:33:52)
	};

	/** Used to pull activities from a session. Used to fetch and display the activities of an archived session. */
	using FFetchActivitiesFunc = TFunction<bool(TArray<TSharedPtr<FConcertClientSessionActivity>>& /*InOutActivities*/, int32& /*OutFetchedCount*/, FText& /*ErrorMsg*/)>;

	/** Used to map an activity to its client. */
	using FGetActivityClientInfoFunc = TFunction<const FConcertClientInfo*(FGuid /*ClientId*/)>;

	/** Returns the transaction event corresponding the specified activity.*/
	using FGetTransactionEvent = TFunction<TFuture<TOptional<FConcertSyncTransactionEvent>>(const FConcertClientSessionActivity& /*Activity*/)>;

	/** Returns the package event corresponding to the package activity. */
	using FGetPackageEvent = TFunction<bool(const FConcertClientSessionActivity& /*Activity*/, FConcertSyncPackageEventMetaData& /*OutEvent*/)>;

	/** Used to overlay a widget over a column widget to add custom functionalities to a row. */
	using FMakeColumnOverlayWidgetFunc = TFunction<TSharedPtr<SWidget>(TWeakPtr<FConcertClientSessionActivity> /*ThisRowActivity*/, const FName& /*ColumnId*/)>;

public:
	SLATE_BEGIN_ARGS(SConcertSessionActivities)
		: _OnFetchActivities()
		, _OnMapActivityToClient()
		, _OnMakeColumnOverlayWidget()
		, _TimeFormat(ETimeFormat::Relative)
		, _ClientAvatarColorColumnVisibility(EVisibility::Hidden)
		, _ClientNameColumnVisibility(EVisibility::Hidden)
		, _OperationColumnVisibility(EVisibility::Hidden)
		, _PackageColumnVisibility(EVisibility::Hidden)
		, _ConnectionActivitiesVisibility(EVisibility::Hidden)
		, _LockActivitiesVisibility(EVisibility::Hidden)
		, _PackageActivitiesVisibility(EVisibility::Visible)
		, _TransactionActivitiesVisibility(EVisibility::Visible)
		, _IgnoredActivitiesVisibility(EVisibility::Hidden)
		, _DetailsAreaVisibility(EVisibility::Hidden)
		, _IsAutoScrollEnabled(false){ }

		/** If bound, invoked to populate the view. */
		SLATE_ARGUMENT(FFetchActivitiesFunc, OnFetchActivities)

		/** If bound, invoked to map an activity to a client.*/
		SLATE_ARGUMENT(FGetActivityClientInfoFunc, OnMapActivityToClient)

		/** If bound, invoked to fill up the package activity details panel. */
		SLATE_ARGUMENT(FGetPackageEvent, OnGetPackageEvent)

		/** If bound, invoked to fill up the transaction activity details panel. */
		SLATE_ARGUMENT(FGetTransactionEvent, OnGetTransactionEvent)

		/** If bound, invoked when generating a row to add an overlay to a column. */
		SLATE_ARGUMENT(FMakeColumnOverlayWidgetFunc, OnMakeColumnOverlayWidget)

		/** Highlight the returned text in the view. */
		SLATE_ATTRIBUTE(FText, HighlightText)

		/** Defines how time should be displayed (relative vs absolute). */
		SLATE_ATTRIBUTE(ETimeFormat, TimeFormat)

		/** Show/hide the column displaying the avatar color of the client who performed the activity. */
		SLATE_ATTRIBUTE(EVisibility, ClientAvatarColorColumnVisibility)

		/** Show/hide the column showing the display name of the client who performed the activity. */
		SLATE_ATTRIBUTE(EVisibility, ClientNameColumnVisibility)

		/** Show/hide the column showing the operation name. */
		SLATE_ATTRIBUTE(EVisibility, OperationColumnVisibility)

		/** Show/hide the column showing the affected package. */
		SLATE_ATTRIBUTE(EVisibility, PackageColumnVisibility)

		/** Show/hide connection activities. */
		SLATE_ATTRIBUTE(EVisibility, ConnectionActivitiesVisibility)

		/** Show/hide lock activities. */
		SLATE_ATTRIBUTE(EVisibility, LockActivitiesVisibility)

		/** Show/hide package activities. */
		SLATE_ATTRIBUTE(EVisibility, PackageActivitiesVisibility)

		/** Show/hide transaction activities. */
		SLATE_ATTRIBUTE(EVisibility, TransactionActivitiesVisibility)

		/** Show/hide ignored activities. */
		SLATE_ATTRIBUTE(EVisibility, IgnoredActivitiesVisibility)

		/** Show/hide the details area widget. (Not to confuse with widget expansion state) */
		SLATE_ARGUMENT(EVisibility, DetailsAreaVisibility)

		/** True to scroll the list down automatically (unless the user manually scrolled the list). */
		SLATE_ARGUMENT(bool, IsAutoScrollEnabled)

		/** Show/hide a message overlay above the activities list explaining why no activities are displayed. */
		SLATE_ATTRIBUTE(FText, NoActivitiesReasonText)
	SLATE_END_ARGS();

	/**
	 * Construct the recovery widget.
	 * @param InArgs The widgets arguments and attributes.
	 */
	void Construct(const FArguments& InArgs);

	/** Requests and consumes more activities if needed and/or possible. */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Returns the activity selected or null if none is selected. */
	TSharedPtr<FConcertClientSessionActivity> GetSelectedActivity() const;

	/** Returns the total number of activities currently stored (no filter applied). */
	int32 GetTotalActivityNum() const { return AllActivities.Num(); }

	/** Returns the number of activities shown. */
	int32 GetDisplayedActivityNum() const { return Activities.Num(); }

	/** Returns the number of activities marked as 'ignored'. */
	int32 GetIgnoredActivityNum() const { return IgnoredActivityNum; }

	/** Returns the most recent activity available, ignoring the current filter. */
	TSharedPtr<FConcertClientSessionActivity> GetMostRecentActivity() const;

	/** Returns true if the column names is the last one (most right one). */
	bool IsLastColumn(const FName& ColumnId) const;

	/** Clears all activities displayed. */
	void Reset();

	/** Append an activity to the view. Used to populate the view from a live session. */
	void Append(TSharedPtr<FConcertClientSessionActivity> Activity);

	/** Request the view to refresh. */
	void RequestRefresh();

	/** Asks the view to update the text search filter. */
	FText UpdateTextFilter(const FText& InFilterText);

private:
	/** Generates a row widget in the table view. */
	TSharedRef<ITableRow> OnGenerateActivityRowWidget(TSharedPtr<FConcertClientSessionActivity> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Fetches more activities. */
	void FetchActivities();

	/** Queries the active set of activity filter flags. */
	EConcertActivityFilterFlags QueryActiveActivityFilters() const;

	/** Invoked when the filter changes and the displayed activity list must be reevaluated. */
	void OnActivityFilterUpdated();

	/** Returns true if the specified activity passes the view filters. */
	bool PassesFilters(const FConcertClientSessionActivity& Activity);

	/** Invoked when the list view scrolled to fetch more data from the activity provider. */
	void OnListViewScrolled(double InScrollOffset);

	/** Invoked when the list view selection changes. */
	void OnListViewSelectionChanged(TSharedPtr<FConcertClientSessionActivity> InActivity, ESelectInfo::Type SelectInfo);

	/** Convert the item into a list of text element used search bars. */
	void PopulateSearchStrings(const FConcertClientSessionActivity& Item, TArray<FString>& OutSearchStrings) const;

	/** Returns whether the details expandable area should be visible or not. */
	EVisibility GetDetailAreaVisibility() const { return DetailsAreaVisibility; }

	/** Manages how much space is used by the 'Details' area with respect to its expansion state. */
	SSplitter::ESizeRule GetDetailsAreaSizeRule() const { return bDetailsAreaExpanded ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent; }

	/** Invoked when the 'Details' expandable area is expanded/collapsed. Affects how the 'Details' area size is accounted in the splitter. */
	void OnDetailsAreaExpansionChanged(bool bExpanded);

	/** Displays what properties/objects changed during a transaction in the details area when a transaction activity is selected in the list. */
	void DisplayTransactionDetails(const FConcertClientSessionActivity& Activity, const FConcertTransactionEventBase& InTransaction);

	/** Displays what changed in a package in the details area when a package activity is selected in the list. */
	void DisplayPackageDetails(const FConcertClientSessionActivity& Activity, int64 PackageRevision, const FConcertPackageInfo& PackageInfo);

	/** Update the detail area to display the proper panel and details. */
	void UpdateDetailArea(TSharedPtr<FConcertClientSessionActivity> InSelectedActivity);

	/** Change the visibility of the details panels, keeping only one visible. */
	void SetDetailsPanelVisibility(const SWidget* VisiblePanel);

	/** Returns the text displayed to the user when there are no details to display. */
	FText GetNoDetailsText() const;

	/** Invoked after all widget ticks get processed. Used to check if the user scrolled or not. */
	void OnPostTick(float);

private:
	/** List of all activities (including the filtered out ones). */
	TArray<TSharedPtr<FConcertClientSessionActivity>> AllActivities;

	/** List of currently displayed activities. */
	TArray<TSharedPtr<FConcertClientSessionActivity>> Activities;

	/** The list view widget displaying the activities. */
	TSharedPtr<SListView<TSharedPtr<FConcertClientSessionActivity>>> ActivityView;

	/** Used to overlay a widget over a column widget (add an extra layer above the normal one) */
	FMakeColumnOverlayWidgetFunc MakeColumnOverlayWidgetFn;

	/** Returns which text should be highlighted. */
	TAttribute<FText> HighlightText;

	/** Whether the time should be displayed as relative (9 seconds ago) or as absolute (July 10, 2019 - 10:20:10) */
	TAttribute<ETimeFormat> TimeFormat;

	/** Whether the client name column is displayed. */
	TAttribute<EVisibility> ClientNameColumnVisibility;

	/** Whether the operation column is displayed. */
	TAttribute<EVisibility> OperationColumnVisibility;

	/** Whether the package column is displayed. */
	TAttribute<EVisibility> PackageColumnVisibility;

	/** Whether the join/leave session activities are displayed. */
	TAttribute<EVisibility> ConnectionActivitiesVisibility;

	/** Whether the lock/unlock activities are displayed.*/
	TAttribute<EVisibility> LockActivitiesVisibility;

	/** Whether the package activities are displayed.*/
	TAttribute<EVisibility> PackageActivitiesVisibility;

	/** Whether the transaction activities are displayed.*/
	TAttribute<EVisibility> TransactionActivitiesVisibility;

	/** Whether the ignored activities are displayed.*/
	TAttribute<EVisibility> IgnoredActivitiesVisibility;

	/** The number of activities flagged as 'ignored' within AllActivities array.*/
	int32 IgnoredActivityNum = 0;

	/** Whether the auto-scrolling is desired (scroll to bottom automatically unless the user scroll himself somewhere). */
	bool bAutoScrollDesired = false;

	/** Whether the user is scrolling (deactivate auto-scrolling). */
	bool bUserScrolling = false;

	/** Defines which activity types are currently filtered out from the view. */
	EConcertActivityFilterFlags ActiveFilterFlags;

	/** Used to fetch more activities from an abstract source. Usually mutually exclusive with Append() function. May not be bound. */
	FFetchActivitiesFunc FetchActivitiesFn;

	/** Used to map an activity endpoint ID to a client. May not be bound. */
	FGetActivityClientInfoFunc GetActivityUserFn;

	/** Used to get the transaction event to display the selected transaction activity details. May not be bound. */
	FGetTransactionEvent GetTransactionEventFn;

	/** Used to get the package event to display the selected package activity details. May not be bound. */
	FGetPackageEvent GetPackageEventFn;

	/** The number of activities to request when scrolling down to request activities on demand. Used with FetchActivitiesFn() for paging. */
	static constexpr int32 ActivitiesPerRequest = 128;

	/** The current desired amount of activities to display. Used for paging with FetchActivitiesFn(). It will grow when the user scrolls down. */
	int32 DesiredActivitiesCount = ActivitiesPerRequest;

	/** True once the activity provider function (FetchActivitiesFn) returns true. */
	bool bAllActivitiesFetched = false;

	/** Utility class used to tokenize and match text displayed in the list view. */
	TSharedPtr<TTextFilter<const FConcertClientSessionActivity&>> SearchTextFilter;

	/** The expandable area under which the activity details are displayed. */
	TSharedPtr<SExpandableArea> ExpandableDetails;

	/** The widget displaying transaction details (if detail area is expanded) when a transaction activity is selected. */
	TSharedPtr<SUndoHistoryDetails> TransactionDetailsPanel;

	/** The widget displaying package details (if detail area is expanded) when a package activity is selected.*/
	TSharedPtr<SPackageDetails> PackageDetailsPanel;

	/** The widget saying they are no detail available (if detail area is expanded) when the activity has no details or no activity is selected. */
	TSharedPtr<SBox> NoDetailsPanel;

	/** The widget displayed when details of a partially sync activity (if detail area is expanded) are being fetched from the server. */
	TSharedPtr<SWidget> LoadingDetailsPanel;

	/** Indicate if the details area should be displayed or not. */
	EVisibility DetailsAreaVisibility = EVisibility::Collapsed;

	/** Keeps the expanded status of the details area. */
	bool bDetailsAreaExpanded = false;

	/** Indicate whether the activity list view was scrolled during a frame. Used to correctly detect auto-scrolling in special cases. */
	bool bActivityViewScrolled = false;
};

/**
 * Manages the various options exposed by SConcertSessionActivities widget such as enabling/disabling filtering,
 * changing the time format, etc.
 */
class CONCERTSYNCUI_API FConcertSessionActivitiesOptions : public TSharedFromThis<FConcertSessionActivitiesOptions>
{
public:
	/** Returns a Menu widgets containing the available options. */
	TSharedRef<SWidget> MakeMenuWidget();

	/** Makes a standard View Options widget, displaying the eye ball icon and showing the possible options. */
	TSharedRef<SWidget> MakeViewOptionsWidget();

	/** Makes a widgets saying how many operations are shown with respect to those filtered out. */
	TSharedRef<SWidget> MakeDisplayedActivityCountWidget(TAttribute<int32> Total, TAttribute<int32> Displayed);

	/** Makes a status bar widget displaying the activity shown and the standard view options button. */
	TSharedRef<SWidget> MakeStatusBar(TAttribute<int32> Total, TAttribute<int32> Displayed);

	/** Returns the time format option. */
	SConcertSessionActivities::ETimeFormat GetTimeFormat() const { return bDisplayRelativeTime ? SConcertSessionActivities::ETimeFormat::Relative : SConcertSessionActivities::ETimeFormat::Absolute; }

	/** Returns whether the connection activities are listed in SConcertSessionActivities. */
	EVisibility GetConnectionActivitiesVisibility() const { return bDisplayConnectionActivities ? EVisibility::Visible : EVisibility::Hidden; }

	/** Returns whether the lock activities are listed in SConcertSessionActivities. */
	EVisibility GetLockActivitiesVisibility() const { return bDisplayLockActivities ? EVisibility::Visible : EVisibility::Hidden; }

	/** Returns whether the package activities are listed in SConcertSessionActivities. */
	EVisibility GetPackageActivitiesVisibility() const { return bDisplayPackageActivities ? EVisibility::Visible : EVisibility::Hidden; }

	/** Returns whether the transaction activities are listed in SConcertSessionActivities. */
	EVisibility GetTransactionActivitiesVisibility() const { return bDisplayTransactionActivities ? EVisibility::Visible : EVisibility::Hidden; }

	/** Returns whether the ignored activities are listed in SConcertSessionActivities. */
	EVisibility GetIgnoredActivitiesVisibility() const { return bDisplayIgnoredActivities ? EVisibility::Visible : EVisibility::Hidden; }

	/** Invoked when an options is togged from the displayed menu widget. */
	void OnOptionToggled(const FName CheckBoxId);

	/** Enables the 'connection activity' filter check box. (Show Connection Activities). */
	bool bEnableConnectionActivityFiltering = true;

	/** Enables the 'lock activity' filter check box. (Show Lock Activities).*/
	bool bEnableLockActivityFiltering = true;

	/** Enables the 'package activity' filter check box (Show Package Activities). */
	bool bEnablePackageActivityFiltering = true;

	/** Enables the 'transaction activity' filter check box (Show Transaction Activities). */
	bool bEnableTransactionActivityFiltering = true;

	/** Enables the 'ignored activity' filter check box (Show Unrecoverable Activities). */
	bool bEnableIgnoredActivityFiltering = false;

	/** Controls whether the time is displayed as absolute or relative. (Display Relative Time). */
	bool bDisplayRelativeTime = true;

	/** If connection filtering is enabled, controls whether connection activities are filtered out. */
	bool bDisplayConnectionActivities = false;

	/** If lock filtering is enabled, controls whether lock activities are filtered out. */
	bool bDisplayLockActivities = false;

	/** If package filtering is enabled, controls whether package activities are filtered out. */
	bool bDisplayPackageActivities = true;

	/** If transaction filtering is enabled, controls whether transaction activities are filtered out.*/
	bool bDisplayTransactionActivities = true;

	/** If ignored activity filtering is enabled, controls whether ignored activities are filtered out.*/
	bool bDisplayIgnoredActivities = false;
};
