// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Session/Activity/SConcertSessionActivities.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FConcertSessionActivitiesOptions;

/** Displays package activities that happened in a concert session. */
class SConcertSessionPackageViewer : public SCompoundWidget
{
public:

	struct FConcertPackageActivity
	{
		FConcertSyncActivity Base;
		FConcertSyncPackageEventData PackageEvent;
	};
	
	SLATE_BEGIN_ARGS(SConcertSessionPackageViewer) {}
		SLATE_EVENT(SConcertSessionActivities::FGetPackageEvent, GetPackageEvent)
		SLATE_EVENT(SConcertSessionActivities::FGetActivityClientInfoFunc, GetClientInfo)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void ResetActivityList();
	void AppendActivity(FConcertSessionActivity Activity);

private:

	TSharedPtr<SConcertSessionActivities> ActivityListView;
	
	/** Controls the activity list view options */
	TSharedPtr<FConcertSessionActivitiesOptions> ActivityListViewOptions;

	/** The widget used to enter the text to search. */
	TSharedPtr<SSearchBox> SearchBox;

	/** The searched text to highlight. */
	FText SearchedText;

	/** Invoked when the text in the search box widget changes. */
	void OnSearchTextChanged(const FText& InSearchText);
	/** Invoked when the text in the search box widget is committed. */
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType);
	/** Returns the text to highlight when the search bar has a text set. */
	FText HighlightSearchedText() const;
};
