// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertSessionPackageViewer.h"

#include "SConcertSessionActivities.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI"

void SConcertSessionPackageViewer::Construct(const FArguments& InArgs)
{
	ActivityListViewOptions = MakeShared<FConcertSessionActivitiesOptions>();
	ActivityListViewOptions->bEnableConnectionActivityFiltering = false;
	ActivityListViewOptions->bEnableLockActivityFiltering = false;
	ActivityListViewOptions->bEnablePackageActivityFiltering = false;
	ActivityListViewOptions->bEnableTransactionActivityFiltering = false;
	
	SAssignNew(ActivityListView, SConcertSessionActivities)
		.OnGetPackageEvent(InArgs._GetPackageEvent)
		.OnFetchActivities(InArgs._FetchInitialActivities)
		.OnMapActivityToClient(InArgs._GetClientInfo)
		.HighlightText(this, &SConcertSessionPackageViewer::HighlightSearchedText)
		.TimeFormat(ActivityListViewOptions.Get(), &FConcertSessionActivitiesOptions::GetTimeFormat)
		.ClientNameColumnVisibility(EVisibility::Visible)
		.ClientAvatarColorColumnVisibility(EVisibility::Collapsed)
		.OperationColumnVisibility(EVisibility::Collapsed)
		.PackageColumnVisibility(EVisibility::Collapsed)
		.ConnectionActivitiesVisibility(EVisibility::Collapsed)
		.LockActivitiesVisibility(EVisibility::Collapsed)
		.PackageActivitiesVisibility(EVisibility::Visible)
		.TransactionActivitiesVisibility(EVisibility::Collapsed)
		.DetailsAreaVisibility(EVisibility::Collapsed)
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
			.OnTextChanged(this, &SConcertSessionPackageViewer::OnSearchTextChanged)
			.OnTextCommitted(this, &SConcertSessionPackageViewer::OnSearchTextCommitted)
			.DelayChangeNotificationsWhileTyping(true)
		]

		+SVerticalBox::Slot()
		[
			ActivityListView.ToSharedRef()
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			ActivityListViewOptions->MakeStatusBar(
				TAttribute<int32>(ActivityListView.Get(), &SConcertSessionActivities::GetTotalActivityNum),
				TAttribute<int32>(ActivityListView.Get(), &SConcertSessionActivities::GetDisplayedActivityNum)
				)
		]
	];
}

void SConcertSessionPackageViewer::ResetActivityList()
{
	ActivityListView->ResetActivityList();
}

void SConcertSessionPackageViewer::AppendActivity(FConcertSessionActivity Activity)
{
	const TSharedRef<FConcertSessionActivity> NewActivity = MakeShared<FConcertSessionActivity>(MoveTemp(Activity));
	ActivityListView->Append(NewActivity);
}

void SConcertSessionPackageViewer::OnSearchTextChanged(const FText& InSearchText)
{
	SearchedText = InSearchText;
	SearchBox->SetError(ActivityListView->UpdateTextFilter(InSearchText));
}

void SConcertSessionPackageViewer::OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType)
{
	if (!InFilterText.EqualTo(SearchedText))
	{
		OnSearchTextChanged(InFilterText);
	}
}

FText SConcertSessionPackageViewer::HighlightSearchedText() const
{
	return SearchedText;
}

#undef LOCTEXT_NAMESPACE
