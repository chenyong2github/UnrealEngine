// Copyright Epic Games, Inc. All Rights Reserved.

#include "Session/History/SEditableSessionHistory.h"

#include "Styling/AppStyle.h"
#include "SNegativeActionButton.h"

#include "Algo/Transform.h"

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
		.SelectionMode(ESelectionMode::Multi)
		.SearchButtonArea()
		[
			SNew(SNegativeActionButton)
			.OnClicked(this, &SEditableSessionHistory::OnClickDeleteActivityButton)
			.ToolTipText(this, &SEditableSessionHistory::GetDeleteActivityToolTip)
			.IsEnabled(this, &SEditableSessionHistory::IsDeleteButtonEnabled)
			.Icon(FAppStyle::GetBrush("Icons.Delete"))
		]
		);
	
	ChildSlot
	[
		SessionHistory.ToSharedRef()
	];
}

FReply SEditableSessionHistory::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		const TSet<TSharedRef<FConcertSessionActivity>> SelectedActivities = SessionHistory->GetSelectedActivities();
		if (CanDeleteActivityFunc.Execute(SelectedActivities).CanDelete())
		{
			DeleteActivityFunc.Execute(SelectedActivities);
		}
		return FReply::Handled();
	}
	
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SEditableSessionHistory::OnClickDeleteActivityButton() const
{
	if (DeleteActivityFunc.IsBound() && CanDeleteActivityFunc.Execute(SessionHistory->GetSelectedActivities()).CanDelete())
	{
		DeleteActivityFunc.Execute(SessionHistory->GetSelectedActivities());
	}
	return FReply::Handled();
}

FText SEditableSessionHistory::GetDeleteActivityToolTip() const
{
	TSet<TSharedRef<FConcertSessionActivity>> SelectedActivities = SessionHistory->GetSelectedActivities();
	if (SelectedActivities.Num() == 0)
	{
		return LOCTEXT("SelectActivityToolTip", "Select some activities to delete from below (multi-select using CTRL + Click).");
	}
	
	const FCanDeleteActivitiesResult CanDeleteActivities = CanDeleteActivityFunc.Execute(SelectedActivities);
	if (CanDeleteActivities.CanDelete())
	{
		SelectedActivities.Sort([](const TSharedRef<FConcertSessionActivity>& First, const TSharedRef<FConcertSessionActivity>& Second) { return First->Activity.ActivityId <= Second->Activity.ActivityId; });
		
		TStringBuilder<256> ActivityBuilder;
		bool bNeedsComma = false;
		for (const TSharedRef<FConcertSessionActivity>& Activity : SelectedActivities)
		{
			if (bNeedsComma)
			{
				ActivityBuilder << ", ";
			}
			ActivityBuilder << Activity->Activity.ActivityId;

			bNeedsComma = true;
		}
		return FText::Format(LOCTEXT("DeleteSelectedActivitiesToolTip", "Delete selected activities from history (IDs: {0})"), FText::FromString(ActivityBuilder.ToString()));
	}
	return FText::Format(LOCTEXT("CannotDeleteSelectedActivitiesToolTip", "Activity cannot be deleted: {0}"), CanDeleteActivities.DeletionReason.GetValue());
}

bool SEditableSessionHistory::IsDeleteButtonEnabled() const
{
	const TSet<TSharedRef<FConcertSessionActivity>> SelectedActivities = SessionHistory->GetSelectedActivities();
	return SelectedActivities.Num() > 0 && CanDeleteActivityFunc.Execute(SelectedActivities).CanDelete();
}

#undef LOCTEXT_NAMESPACE
