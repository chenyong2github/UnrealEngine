// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertClientWorkspace.h"
#include "ConcertSyncSessionTypes.h"
#include "Async/Future.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IConcertSyncClient;
class SSearchBox;
class SConcertSessionActivities;
class FConcertSessionActivitiesOptions;

class SSessionHistory : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSessionHistory) {}
		SLATE_ARGUMENT(FName, PackageFilter)
	SLATE_END_ARGS()

	/**
	* Constructs the Session History widget.
	*
	* @param InArgs The Slate argument list.
	* @param ConstructUnderMajorTab The major tab which will contain the Session History widget.
	* @param InConcertSyncClient Pointer on the concert sync client.
	*/
	void Construct(const FArguments& InArgs, TSharedPtr<IConcertSyncClient> InConcertSyncClient);

	/** Fetches the activities and updates the UI. */
	void Refresh();

private:
	/** Callback for selecting an activity in the list view. */
	void HandleSelectionChanged(TSharedPtr<FConcertClientSessionActivity> InSessionActivity, ESelectInfo::Type SelectInfo);

	/** Fetches activities from the server and updates the list view. */
	void ReloadActivities();

	/** Callback for handling the a new or updated activity item. */ 
	void HandleActivityAddedOrUpdated(const FConcertClientInfo& InClientInfo, const FConcertSyncActivity& InActivity, const FStructOnScope& InActivitySummary);

	/** Callback for handling the startup of a workspace.  */
	void HandleWorkspaceStartup(const TSharedPtr<IConcertClientWorkspace>& NewWorkspace);

	/** Callback for handling the shutdown of a workspace. */
	void HandleWorkspaceShutdown(const TSharedPtr<IConcertClientWorkspace>& WorkspaceShuttingDown);

	/** Registers callbacks with the current workspace. */
	void RegisterWorkspaceHandler();

	/** Invoked when the text in the search box widget changes. */
	void OnSearchTextChanged(const FText& InSearchText);

	/** Invoked when the text in the search box widget is committed. */
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType);

	/** Returns the text to highlight when the search bar has a text set. */
	FText HighlightSearchedText() const;

	/** Returns the specified package event (without the package data itself) if available. */
	bool GetPackageEvent(const FConcertClientSessionActivity& Activity, FConcertSyncPackageEventMetaData& OutPackageEvent) const;

	/** Returns the specified package event if available. */
	TFuture<TOptional<FConcertSyncTransactionEvent>> GetTransactionEvent(const FConcertClientSessionActivity& Activity) const;

private:

	/** Maximum number of activities displayed on screen. */ 
	static const int64 MaximumNumberOfActivities = 1000;
	
	/** Holds the map of endpoint IDs to client info. */
	TMap<FGuid, FConcertClientInfo> EndpointClientInfoMap;

	/** Holds the map of activity IDs to Concert activities. */
	TMap<int64, TSharedPtr<FConcertClientSessionActivity>> ActivityMap;

	/** Display the activity list. */
	TSharedPtr<SConcertSessionActivities> ActivityListView;

	/** Controls the activity list view options */
	TSharedPtr<FConcertSessionActivitiesOptions> ActivityListViewOptions;

	/** Holds a weak pointer to the current workspace. */ 
	TWeakPtr<IConcertClientWorkspace> Workspace;

	/** The widget used to enter the text to search. */
	TSharedPtr<SSearchBox> SearchBox;

	/** The searched text to highlight. */
	FText SearchedText;

	/** Used to limit activities to a given package only. */
	FName PackageNameFilter;
};