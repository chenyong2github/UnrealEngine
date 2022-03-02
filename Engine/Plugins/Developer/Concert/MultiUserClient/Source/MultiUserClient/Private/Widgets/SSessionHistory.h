// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SConcertSessionActivities.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSearchBox;
class SConcertSessionActivities;
class FConcertSessionActivitiesOptions;

class SSessionHistory : public SCompoundWidget
{
public:

	/** Maximum number of activities displayed on screen. */ 
	static constexpr int64 MaximumNumberOfActivities = 1000;
	
	SLATE_BEGIN_ARGS(SSessionHistory) {}
		SLATE_ARGUMENT(FName, PackageFilter)
		SLATE_ARGUMENT(SConcertSessionActivities::FGetPackageEvent, GetPackageEvent)
		SLATE_ARGUMENT(SConcertSessionActivities::FGetTransactionEvent, GetTransactionEvent)
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

private:

	/** Invoked when the text in the search box widget changes. */
	void OnSearchTextChanged(const FText& InSearchText);

	/** Invoked when the text in the search box widget is committed. */
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType);

	/** Returns the text to highlight when the search bar has a text set. */
	FText HighlightSearchedText() const;

	/** Returns the specified package event (without the package data itself) if available. */
	bool GetPackageEvent(const FConcertSessionActivity& Activity, FConcertSyncPackageEventMetaData& OutPackageEvent) const;

	/** Returns the specified package event if available. */
	TFuture<TOptional<FConcertSyncTransactionEvent>> GetTransactionEvent(const FConcertSessionActivity& Activity) const;

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
};