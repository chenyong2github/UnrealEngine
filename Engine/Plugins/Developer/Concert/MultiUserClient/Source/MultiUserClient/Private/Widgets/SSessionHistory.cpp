// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSessionHistory.h"
#include "IConcertSession.h"
#include "IConcertSyncClient.h"
#include "ConcertTransactionEvents.h"
#include "ConcertWorkspaceData.h"
#include "ConcertMessageData.h"
#include "ConcertMessages.h"
#include "Editor/Transactor.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "SConcertSessionActivities.h"

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

void SSessionHistory::Construct(const FArguments& InArgs)
{
	PackageNameFilter = InArgs._PackageFilter;

	ActivityMap.Reserve(MaximumNumberOfActivities);
	ActivityListViewOptions = MakeShared<FConcertSessionActivitiesOptions>();

	SAssignNew(ActivityListView, SConcertSessionActivities)
		.OnGetPackageEvent(InArgs._GetPackageEvent)
		.OnGetTransactionEvent(InArgs._GetTransactionEvent)
		.OnMapActivityToClient([this](FGuid ClientId){ return EndpointClientInfoMap.Find(ClientId); })
		.HighlightText(this, &SSessionHistory::HighlightSearchedText)
		.TimeFormat(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetTimeFormat)
		.ClientNameColumnVisibility(EVisibility::Visible)
		.ClientAvatarColorColumnVisibility(EVisibility::Visible)
		.OperationColumnVisibility(EVisibility::Visible)
		.PackageColumnVisibility(EVisibility::Collapsed)
		.ConnectionActivitiesVisibility(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetConnectionActivitiesVisibility)
		.LockActivitiesVisibility(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetLockActivitiesVisibility)
		.PackageActivitiesVisibility(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetPackageActivitiesVisibility)
		.TransactionActivitiesVisibility(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetTransactionActivitiesVisibility)
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
}

void SSessionHistory::ReloadActivities(TMap<FGuid, FConcertClientInfo> InEndpointClientInfoMap, TArray<FConcertSessionActivity> InFetchedActivities)
{
	EndpointClientInfoMap = MoveTemp(InEndpointClientInfoMap);
	ActivityMap.Reset();
	ActivityListView->Reset(); // Careful, don't reset the shared ptr.

	for (FConcertSessionActivity& FetchedActivity : InFetchedActivities)
	{
		if (ConcertSessionHistoryUI::PackageNamePassesFilter(PackageNameFilter, FetchedActivity.ActivitySummary))
		{
			TSharedRef<FConcertSessionActivity> NewActivity = MakeShared<FConcertSessionActivity>(MoveTemp(FetchedActivity));
			ActivityMap.Add(NewActivity->Activity.ActivityId, NewActivity);
			ActivityListView->Append(NewActivity);
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

		if (TSharedPtr<FConcertSessionActivity> ExistingActivity = ActivityMap.FindRef(InActivity.ActivityId))
		{
			ExistingActivity->Activity = InActivity;
			ExistingActivity->ActivitySummary = MoveTemp(ActivitySummary);
			ActivityListView->RequestRefresh();
		}
		else
		{
			TSharedRef<FConcertSessionActivity> NewActivity = MakeShared<FConcertSessionActivity>();
			NewActivity->Activity = InActivity;
			NewActivity->ActivitySummary = MoveTemp(ActivitySummary);

			ActivityMap.Add(NewActivity->Activity.ActivityId, NewActivity);
			ActivityListView->Append(NewActivity);
		}
	}
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

#undef LOCTEXT_NAMESPACE /* SSessionHistory */
