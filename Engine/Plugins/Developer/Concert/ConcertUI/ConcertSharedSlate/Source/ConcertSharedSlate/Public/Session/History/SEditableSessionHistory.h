// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSessionHistory;
struct FConcertSessionActivity;

/** Allows activities in the session history to be deleted. */
class CONCERTSHAREDSLATE_API SEditableSessionHistory : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SSessionHistory>, FMakeSessionHistory, SSessionHistory::FArguments)
	DECLARE_DELEGATE_RetVal_OneParam(bool, FCanDeleteActivity, const TSharedRef<FConcertSessionActivity>&)
	DECLARE_DELEGATE_OneParam(FRequestDeleteActivity, const TSharedRef<FConcertSessionActivity>&)

	SLATE_BEGIN_ARGS(SEditableSessionHistory)
	{}
		SLATE_EVENT(FMakeSessionHistory, MakeSessionHistory)
		SLATE_EVENT(FCanDeleteActivity, CanDeleteActivity)
		SLATE_EVENT(FRequestDeleteActivity, DeleteActivity)
		SLATE_NAMED_SLOT(FArguments, StatusBar)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	TSharedPtr<SSessionHistory> SessionHistory;

	FCanDeleteActivity CanDeleteActivityFunc;
	FRequestDeleteActivity DeleteActivityFunc;

	/** Creates a delete button that is overlayed over the summary column*/
	TSharedPtr<SWidget> MakeSummaryColumnDeleteButton(TWeakPtr<FConcertSessionActivity> RowActivity, const FName& ColumnId) const;
	FReply OnClickDeleteActivityButton(TWeakPtr<FConcertSessionActivity> RowActivity) const;
	FText GetDeleteActivityToolTip(TWeakPtr<FConcertSessionActivity> RowActivity) const;
	bool CanDeleteActivity(TWeakPtr<FConcertSessionActivity> RowActivity) const;
};
