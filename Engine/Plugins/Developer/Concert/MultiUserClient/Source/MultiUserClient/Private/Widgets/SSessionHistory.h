// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertClientWorkspace.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IConcertSyncClient;
struct FConcertTransactionEventBase;

class SUndoHistoryDetails;
class SPackageDetails;
class SExpandableArea;
class SScrollBar;
class SConcertScrollBox;

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
	
	/** Generates a new event row. */
	TSharedRef<ITableRow> HandleGenerateRow(TSharedPtr<FConcertClientSessionActivity> InSessionActivity, const TSharedRef<STableViewBase>& OwnerTable) const;

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

	/** Open the details section and display transaction details. */
	void DisplayTransactionDetails(const FConcertTransactionEventBase& InTransaction, const FString& InTransactionTitle);

	/** Open the package details section and display the package details. */
	void DisplayPackageDetails(const FConcertPackageInfo& InPackageInfo, const int64 InRevision, const FString& InModifiedBy);

	/** Manages how much space is used by the 'Details' area with respect to its expansion state. */
	SSplitter::ESizeRule GetDetailsAreaSizeRule() const { return bDetailAreaExpanded ? SSplitter::ESizeRule::FractionOfParent : SSplitter::ESizeRule::SizeToContent; }

	/** Invoked when the 'Details' expandable area is expanded/collapsed. Affects how the 'Details' area size is accounted in the splitter. */
	void OnDetailsAreaExpansionChanged(bool bExpanded) { bDetailAreaExpanded = bExpanded; }

private:

	/** Maximum number of activities displayed on screen. */ 
	static const int64 MaximumNumberOfActivities = 1000;
	
	/** Holds the map of endpoint IDs to client info. */
	TMap<FGuid, FConcertClientInfo> EndpointClientInfoMap;

	/** Holds the map of activity IDs to Concert activities. */
	TMap<int64, TSharedPtr<FConcertClientSessionActivity>> ActivityMap;

	/** Holds the list of Concert activities. */
	TArray<TSharedPtr<FConcertClientSessionActivity>> Activities;

	/** Holds an instance of an undo history details panel. */
	TSharedPtr<SUndoHistoryDetails> TransactionDetails;

	TSharedPtr<SPackageDetails> PackageDetails;

	/** Holds activities. */
	TSharedPtr<SListView<TSharedPtr<FConcertClientSessionActivity>>> ActivityListView;

	/** Holds the expandable area containing details about a given activity. */
	TSharedPtr<SExpandableArea> ExpandableDetails;

	/** Holds the history log scroll bar. */
	TSharedPtr<SConcertScrollBox> ScrollBox;

	/** Holds a weak pointer to the current workspace. */ 
	TWeakPtr<IConcertClientWorkspace> Workspace;

	/** Holds the session history scroll bar. */
	TSharedPtr<SScrollBar> ScrollBar;

	FName PackageNameFilter;

	/** Keeps the expand status of the details area. */
	bool bDetailAreaExpanded = false;

};