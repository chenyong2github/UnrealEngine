// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Session/Activity/SConcertSessionActivities.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSearchBox;
class FConcertSessionActivitiesOptions;

class CONCERTSHAREDSLATE_API SSessionHistory : public SCompoundWidget
{
public:

	/** Maximum number of activities displayed on screen. */ 
	static constexpr int64 MaximumNumberOfActivities = 1000;
	
	SLATE_BEGIN_ARGS(SSessionHistory) {}
		SLATE_ARGUMENT(FName, PackageFilter)
		SLATE_EVENT(SConcertSessionActivities::FGetPackageEvent, GetPackageEvent)
		SLATE_EVENT(SConcertSessionActivities::FGetTransactionEvent, GetTransactionEvent)
		/** If bound, invoked when generating a row to add an overlay to a column. */
		SLATE_EVENT(SConcertSessionActivities::FMakeColumnOverlayWidgetFunc, OnMakeColumnOverlayWidget)
	
		/** Optional snapshot to restore column visibilities with */
		SLATE_ARGUMENT(FColumnVisibilitySnapshot, ColumnVisibilitySnapshot)
		/** Called whenever the column visibility changes and should be saved */
		SLATE_EVENT(UE::ConcertSharedSlate::FSaveColumnVisibilitySnapshot, SaveColumnVisibilitySnapshot)
	SLATE_END_ARGS()

	/**
	* Constructs the Session History widget.
	*
	* @param InArgs The Slate argument list.
	* @param ConstructUnderMajorTab The major tab which will contain the Session History widget.
	* @param InConcertSyncClient Pointer on the concert sync client.
	*/
	void Construct(const FArguments& InArgs);

	/** Fetches activities from the server and updates the list view. */
	void ReloadActivities(TMap<FGuid, FConcertClientInfo> EndpointClientInfoMap, TArray<FConcertSessionActivity> FetchedActivities);
	
	/** Callback for handling the a new or updated activity item. */ 
	void HandleActivityAddedOrUpdated(const FConcertClientInfo& InClientInfo, const FConcertSyncActivity& InActivity, const FStructOnScope& InActivitySummary);

	bool IsLastColumn(FName ColumnId) const { return ActivityListView->IsLastColumn(ColumnId); }

	void OnColumnVisibilitySettingsChanged(const FColumnVisibilitySnapshot& ColumnSnapshot);

private:

	/** Invoked when the text in the search box widget changes. */
	void OnSearchTextChanged(const FText& InSearchText);

	/** Invoked when the text in the search box widget is committed. */
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType);

	/** Returns the text to highlight when the search bar has a text set. */
	FText HighlightSearchedText() const;

private:
	
	/** Holds the map of endpoint IDs to client info. */
	TMap<FGuid, FConcertClientInfo> EndpointClientInfoMap;

	/** Holds the map of activity IDs to Concert activities. */
	TMap<int64, TSharedPtr<FConcertSessionActivity>> ActivityMap;

	/** Display the activity list. */
	TSharedPtr<SConcertSessionActivities> ActivityListView;

	/** Controls the activity list view options */
	TSharedPtr<FConcertSessionActivitiesOptions> ActivityListViewOptions;

	/** The widget used to enter the text to search. */
	TSharedPtr<SSearchBox> SearchBox;

	/** The searched text to highlight. */
	FText SearchedText;

	/** Used to limit activities to a given package only. */
	FName PackageNameFilter;

	TOptional<FConcertClientInfo> GetClientInfo(FGuid Guid) const;
};