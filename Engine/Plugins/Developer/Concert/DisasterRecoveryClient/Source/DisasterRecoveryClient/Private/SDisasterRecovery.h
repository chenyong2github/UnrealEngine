// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertFrontendUtils.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IConcertSyncClient;
struct FConcertClientSessionActivity;

/**
 * Displays the list of activities available for recovery and let the user select what should or shouldn't be recovered.
 */
class SDisasterRecovery : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDisasterRecovery) { }
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS();

	/**
	 * Construct the recovery widget.
	 * @param InArgs The widgets arguments and attributes.
	 * @param Activities The list of recoverable activities to display.
	 */
	void Construct(const FArguments& InArgs, TArray<TSharedPtr<FConcertClientSessionActivity>> InActivities);

	/** Returns the activity, selected by the user, up to which the assets should be recovered or null to prevent recovery. */
	TSharedPtr<FConcertClientSessionActivity> GetRecoverThroughItem() { return RecoveryThroughItem; }

private:
	TSharedRef<ITableRow> OnGenerateActivityRowWidget(TSharedPtr<FConcertClientSessionActivity> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnSearchTextChanged(const FText& InFilterText);
	void OnViewOptionCheckBoxToggled(const FName ItemName);
	FReply OnCancelRecoveryClicked();
	FReply OnRecoverClicked();
	FReply OnRecoverAllClicked();
	FText HighlightSearchText() const;
	void RecoverThrough(TSharedPtr<FConcertClientSessionActivity> Item);

	/** Close the windows hosting this recovery widget. */
	void DismissWindow();

private:
	TArray<TSharedPtr<FConcertClientSessionActivity>> Activities;
	TSharedPtr<SListView<TSharedPtr<FConcertClientSessionActivity>>> ActivityView;
	TSharedPtr<FConcertClientSessionActivity> RecoveryThroughItem;
	TWeakPtr<SWindow> ParentWindow;
	TSharedPtr<SSearchBox> SearchBox;
	FText SearchText;
	bool bDisplayRelativeTime = true;
};

/**
 * Displays the summary of an activity recorded and recoverable in the SDisasterRecovery list view.
 */
class SDisasterRecoveryActivityRow : public SMultiColumnTableRow<TSharedPtr<FConcertClientSessionActivity>>
{
public:
	typedef TFunction<void(TSharedPtr<FConcertClientSessionActivity>)> FRecoverFunc;

	SLATE_BEGIN_ARGS(SDisasterRecoveryActivityRow)
		: _RecoverButtonVisibility(EVisibility::Hidden)
		, _DisplayRelativeTime(true)
		, _OnRecoverFunc()
		, _HighlightText()
	{
	}

	SLATE_ATTRIBUTE(EVisibility, RecoverButtonVisibility) // The button at the end of the line in the list view.
	SLATE_ATTRIBUTE(bool, DisplayRelativeTime)
	SLATE_ARGUMENT(FRecoverFunc, OnRecoverFunc)
	SLATE_ATTRIBUTE(FText, HighlightText)

	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TSharedPtr<FConcertClientSessionActivity> InItem, const TSharedRef<STableViewBase>& InOwnerTableView);
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;
	FReply OnRecoverClicked();

	FText FormatEventDateTime() const;

private:
	TWeakPtr<FConcertClientSessionActivity> Item;
	TAttribute<EVisibility> RecoverButtonVisibility;
	TAttribute<bool> DisplayRelativeTime;
	FText AbsoluteDateTime;
	FRecoverFunc OnRecoverFunc;
	TAttribute<FText> HighlightText;
};
