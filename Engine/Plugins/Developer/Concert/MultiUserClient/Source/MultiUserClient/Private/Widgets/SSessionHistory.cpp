// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SSessionHistory.h"
#include "IConcertSession.h"
#include "IConcertSyncClient.h"
#include "ConcertTransactionEvents.h"
#include "ConcertWorkspaceData.h"
#include "ConcertMessageData.h"
#include "ConcertMessages.h"
#include "Editor/Transactor.h"
#include "EditorStyleSet.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "SConcertSessionActivities.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "SSessionHistory"

namespace ConcertSessionHistoryUI
{
	bool PackageNamePassesFilter(const FName& PackageNameFilter, const TStructOnScope<FConcertSyncActivitySummary>& InActivitySummary)
	{
		if (PackageNameFilter.IsNone())
		{
			return true;
		}

		if (const FConcertSyncLockActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncLockActivitySummary>())
		{
			return Summary->PrimaryPackageName == PackageNameFilter;
		}

		if (const FConcertSyncTransactionActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncTransactionActivitySummary>())
		{
			return Summary->PrimaryPackageName == PackageNameFilter;
		}

		if (const FConcertSyncPackageActivitySummary* Summary = InActivitySummary.Cast<FConcertSyncPackageActivitySummary>())
		{
			return Summary->PackageName == PackageNameFilter;
		}

		return false;
	}
}

void SSessionHistory::Construct(const FArguments& InArgs, TSharedPtr<IConcertSyncClient> InConcertSyncClient)
{
	PackageNameFilter = InArgs._PackageFilter;

	ActivityMap.Reserve(MaximumNumberOfActivities);
	ActivityListViewOptions = MakeShared<FConcertSessionActivitiesOptions>();

	SAssignNew(ActivityListView, SConcertSessionActivities)
		.OnGetPackageEvent([this](const FConcertClientSessionActivity& Activity) { return GetPackageEvent(Activity); })
		.OnGetTransactionEvent([this](const FConcertClientSessionActivity& Activity) { return GetTransactionEvent(Activity); })
		.OnMapActivityToClient([this](FGuid ClientId){ return EndpointClientInfoMap.Find(ClientId); })
		.HighlightText(this, &SSessionHistory::HighlightSearchedText)
		.TimeFormat(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetTimeFormat)
		.ClientNameColumnVisibility(EVisibility::Visible)
		.ClientAvatarColorColumnVisibility(EVisibility::Visible)
		.OperationColumnVisibility(EVisibility::Visible)
		.PackageColumnVisibility(EVisibility::Collapsed)
		.ConnectionActivitiesVisibility(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetConnectionActivitiesVisibility)
		.LockActivitiesVisibility(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetLockActivitiesVisibility)
		.DetailsAreaVisibility(EVisibility::Visible)
		.IsAutoScrollEnabled(true);

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(1, 1)
		[
			SAssignNew(SearchBox, SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search..."))
			.OnTextChanged(this, &SSessionHistory::OnSearchTextChanged)
			.OnTextCommitted(this, &SSessionHistory::OnSearchTextCommitted)
			.DelayChangeNotificationsWhileTyping(true)
		]

		+SVerticalBox::Slot()
		[
			ActivityListView.ToSharedRef()
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 3)
		[
			SNew(SSeparator)
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4, 0, 4, 3)
		[
			ActivityListViewOptions->MakeStatusBar(
				TAttribute<int32>(ActivityListView.Get(), &SConcertSessionActivities::GetTotalActivityNum),
				TAttribute<int32>(ActivityListView.Get(), &SConcertSessionActivities::GetDisplayedActivityNum))
		]
	];

	if (InConcertSyncClient.IsValid())
	{
		InConcertSyncClient->OnWorkspaceStartup().AddSP(this, &SSessionHistory::HandleWorkspaceStartup);
		InConcertSyncClient->OnWorkspaceShutdown().AddSP(this, &SSessionHistory::HandleWorkspaceShutdown);

		if (TSharedPtr<IConcertClientWorkspace> WorkspacePtr = InConcertSyncClient->GetWorkspace())
		{
			Workspace = WorkspacePtr;
			RegisterWorkspaceHandler();
			ReloadActivities();
		}
	}
}

void SSessionHistory::Refresh()
{
	ReloadActivities();
}

void SSessionHistory::OnSearchTextChanged(const FText& InSearchText)
{
	SearchedText = InSearchText;
	SearchBox->SetError(ActivityListView->UpdateTextFilter(InSearchText));
}

void SSessionHistory::OnSearchTextCommitted(const FText& InSearchText, ETextCommit::Type CommitType)
{
	if (!InSearchText.EqualTo(SearchedText))
	{
		OnSearchTextChanged(InSearchText);
	}
}

FText SSessionHistory::HighlightSearchedText() const
{
	return SearchedText;
}

void SSessionHistory::ReloadActivities()
{
	EndpointClientInfoMap.Reset();
	ActivityMap.Reset();
	ActivityListView->Reset(); // Careful, don't reset the shared ptr.

	if (TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin())
	{
		const int64 LastActivityId = WorkspacePtr->GetLastActivityId();
		const int64 FirstActivityIdToFetch = FMath::Max<int64>(1, LastActivityId - MaximumNumberOfActivities);

		TArray<FConcertClientSessionActivity> FetchedActivities;
		WorkspacePtr->GetActivities(FirstActivityIdToFetch, MaximumNumberOfActivities, EndpointClientInfoMap, FetchedActivities);

		for (FConcertClientSessionActivity& FetchedActivity : FetchedActivities)
		{
			if (ConcertSessionHistoryUI::PackageNamePassesFilter(PackageNameFilter, FetchedActivity.ActivitySummary))
			{
				TSharedRef<FConcertClientSessionActivity> NewActivity = MakeShared<FConcertClientSessionActivity>(MoveTemp(FetchedActivity));
				ActivityMap.Add(NewActivity->Activity.ActivityId, NewActivity);
				ActivityListView->Append(NewActivity);
			}
		}
	}
}

void SSessionHistory::HandleActivityAddedOrUpdated(const FConcertClientInfo& InClientInfo, const FConcertSyncActivity& InActivity, const FStructOnScope& InActivitySummary)
{
	TStructOnScope<FConcertSyncActivitySummary> ActivitySummary;
	ActivitySummary.InitializeFromChecked(InActivitySummary);

	if (ConcertSessionHistoryUI::PackageNamePassesFilter(PackageNameFilter, ActivitySummary))
	{
		EndpointClientInfoMap.Add(InActivity.EndpointId, InClientInfo);

		if (TSharedPtr<FConcertClientSessionActivity> ExistingActivity = ActivityMap.FindRef(InActivity.ActivityId))
		{
			ExistingActivity->Activity = InActivity;
			ExistingActivity->ActivitySummary = MoveTemp(ActivitySummary);
			ActivityListView->RequestRefresh();
		}
		else
		{
			TSharedRef<FConcertClientSessionActivity> NewActivity = MakeShared<FConcertClientSessionActivity>();
			NewActivity->Activity = InActivity;
			NewActivity->ActivitySummary = MoveTemp(ActivitySummary);

			ActivityMap.Add(NewActivity->Activity.ActivityId, NewActivity);
			ActivityListView->Append(NewActivity);
		}
	}
}

void SSessionHistory::HandleWorkspaceStartup(const TSharedPtr<IConcertClientWorkspace>& NewWorkspace)
{
	Workspace = NewWorkspace;
	RegisterWorkspaceHandler();
}

void SSessionHistory::HandleWorkspaceShutdown(const TSharedPtr<IConcertClientWorkspace>& WorkspaceShuttingDown)
{
	if (WorkspaceShuttingDown == Workspace)
	{
		Workspace.Reset();
		ReloadActivities();
	}
}

void SSessionHistory::RegisterWorkspaceHandler()
{
	TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin();
	if (WorkspacePtr.IsValid())
	{
		WorkspacePtr->OnActivityAddedOrUpdated().AddSP(this, &SSessionHistory::HandleActivityAddedOrUpdated);
		WorkspacePtr->OnWorkspaceSynchronized().AddSP(this, &SSessionHistory::ReloadActivities);
	}
}

TFuture<TOptional<FConcertSyncPackageEvent>> SSessionHistory::GetPackageEvent(const FConcertClientSessionActivity& Activity) const
{
	if (TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin())
	{
		// Don't request the package data, the widget only display the meta-data.
		return WorkspacePtr->FindOrRequestPackageEvent(Activity.Activity.EventId, /*bMetaDataOnly*/true);
	}

	return MakeFulfilledPromise<TOptional<FConcertSyncPackageEvent>>().GetFuture(); // The data is not available.
}

TFuture<TOptional<FConcertSyncTransactionEvent>> SSessionHistory::GetTransactionEvent(const FConcertClientSessionActivity& Activity) const
{
	if (TSharedPtr<IConcertClientWorkspace> WorkspacePtr = Workspace.Pin())
	{
		// Ask to get the full transaction to display which properties changed.
		return WorkspacePtr->FindOrRequestTransactionEvent(Activity.Activity.EventId, /*bMetaDataOnly*/false);
	}

	return MakeFulfilledPromise<TOptional<FConcertSyncTransactionEvent>>().GetFuture();
}


#undef LOCTEXT_NAMESPACE /* SSessionHistory */
