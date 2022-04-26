// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SSessionHistory;
struct FConcertSessionActivity;

struct FCanDeleteActivitiesResult
{
	TOptional<FText> DeletionReason;

	static FCanDeleteActivitiesResult Yes() { return FCanDeleteActivitiesResult(); }
	static FCanDeleteActivitiesResult No(FText Reason) { return FCanDeleteActivitiesResult(MoveTemp(Reason)); }

	bool CanDelete() const { return !DeletionReason.IsSet(); }

	explicit FCanDeleteActivitiesResult(TOptional<FText> DeletionReason = {})
		: DeletionReason(DeletionReason)
	{}
};

/** Allows activities in the session history to be deleted. */
class CONCERTSHAREDSLATE_API SEditableSessionHistory : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SSessionHistory>, FMakeSessionHistory, SSessionHistory::FArguments)
	DECLARE_DELEGATE_RetVal_OneParam(FCanDeleteActivitiesResult, FCanDeleteActivities, const TSet<TSharedRef<FConcertSessionActivity>>& /*Activities*/)
	DECLARE_DELEGATE_OneParam(FRequestDeleteActivities, const TSet<TSharedRef<FConcertSessionActivity>>&)

	SLATE_BEGIN_ARGS(SEditableSessionHistory)
	{}
		SLATE_EVENT(FMakeSessionHistory, MakeSessionHistory)
		SLATE_EVENT(FCanDeleteActivities, CanDeleteActivity)
		SLATE_EVENT(FRequestDeleteActivities, DeleteActivity)
		SLATE_NAMED_SLOT(FArguments, StatusBar)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	
	//~ Begin SWidget Interface
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End SWidget Interface
	
private:

	TSharedPtr<SSessionHistory> SessionHistory;

	FCanDeleteActivities CanDeleteActivityFunc;
	FRequestDeleteActivities DeleteActivityFunc;

	FReply OnClickDeleteActivityButton() const;
	FText GetDeleteActivityToolTip() const;
	bool IsDeleteButtonEnabled() const;
};
