// Copyright Epic Games, Inc. All Rights Reserved.

#include "Session/History/SEditableSessionHistory.h"

#include "EditorStyleSet.h"
#include "SNegativeActionButton.h"
#include "Session/History/SSessionHistory.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "SEditableSessionHistory"

void SEditableSessionHistory::Construct(const FArguments& InArgs)
{
	check(InArgs._MakeSessionHistory.IsBound() && InArgs._CanDeleteActivity.IsBound());
	CanDeleteActivityFunc = InArgs._CanDeleteActivity;
	DeleteActivityFunc = InArgs._DeleteActivity;
	
	SessionHistory = InArgs._MakeSessionHistory.Execute(
			SSessionHistory::FArguments()
			.OnMakeColumnOverlayWidget(this, &SEditableSessionHistory::MakeSummaryColumnDeleteButton)
			);
	
	ChildSlot
	[
		SessionHistory.ToSharedRef()
	];
}

TSharedPtr<SWidget> SEditableSessionHistory::MakeSummaryColumnDeleteButton(TWeakPtr<FConcertSessionActivity> RowActivity, const FName& ColumnId) const
{
	if (!SessionHistory->IsLastColumn(ColumnId))
	{
		return nullptr;
	}
	
	return SNew(SBox)
		.Padding(FMargin(1, 1))
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SNegativeActionButton)
			.OnClicked(this, &SEditableSessionHistory::OnClickDeleteActivityButton, RowActivity)
			.ToolTipText(TAttribute<FText>::CreateLambda([this, RowActivity](){ return GetDeleteActivityToolTip(RowActivity);}))
			.IsEnabled(TAttribute<bool>::CreateLambda([this, RowActivity](){ return CanDeleteActivity(RowActivity);}))
			.Icon(FEditorStyle::GetBrush("Icons.Delete"))
		];
}

FReply SEditableSessionHistory::OnClickDeleteActivityButton(TWeakPtr<FConcertSessionActivity> RowActivity) const
{
	if (DeleteActivityFunc.IsBound() && CanDeleteActivity(RowActivity))
	{
		DeleteActivityFunc.Execute(RowActivity.Pin().ToSharedRef());
	}
	return FReply::Handled();
}

FText SEditableSessionHistory::GetDeleteActivityToolTip(TWeakPtr<FConcertSessionActivity> RowActivity) const
{
	return CanDeleteActivity(RowActivity) ?
		LOCTEXT("DeleteActivityDescription", "Delete activity from history")
		:
		LOCTEXT("CannotDeleteActivityDescription", "Activity cannot be deleted from history");
}

bool SEditableSessionHistory::CanDeleteActivity(TWeakPtr<FConcertSessionActivity> RowActivity) const
{
	if (const TSharedPtr<FConcertSessionActivity> Pinned = RowActivity.Pin())
	{
		return CanDeleteActivityFunc.Execute(Pinned.ToSharedRef());
	}

	checkNoEntry();
	return false;
}

#undef LOCTEXT_NAMESPACE
