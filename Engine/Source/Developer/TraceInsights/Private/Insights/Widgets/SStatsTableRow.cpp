// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SStatsTableRow.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/StatsViewColumnFactory.h"
#include "Insights/Widgets/SStatsTableCell.h"
#include "Insights/Widgets/SStatsViewTooltip.h"

#define LOCTEXT_NAMESPACE "SStatsView"

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SStatsTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	OnShouldBeEnabled = InArgs._OnShouldBeEnabled;
	IsColumnVisibleDelegate = InArgs._OnIsColumnVisible;
	GetColumnOutlineHAlignmentDelegate = InArgs._OnGetColumnOutlineHAlignmentDelegate;
	SetHoveredTableCellDelegate = InArgs._OnSetHoveredTableCell;

	HighlightText = InArgs._HighlightText;
	HighlightedNodeName = InArgs._HighlightedNodeName;

	StatsNodePtr = InArgs._StatsNodePtr;

	RowToolTip = MakeShareable(new SStatsCounterTableRowToolTip(StatsNodePtr));

	SetEnabled(TAttribute<bool>(this, &SStatsTableRow::HandleShouldBeEnabled));

	SMultiColumnTableRow<FStatsNodePtr>::Construct(SMultiColumnTableRow<FStatsNodePtr>::FArguments(), InOwnerTableView);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SStatsTableRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	return
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)

		+SOverlay::Slot()
		.Padding(0.0f)
		[
			SNew(SImage)
			.Image(FEditorStyle::GetBrush("Profiler.LineGraphArea"))
			.ColorAndOpacity(this, &SStatsTableRow::GetBackgroundColorAndOpacity)
		]

		+SOverlay::Slot()
		.Padding(0.0f)
		[
			SNew(SImage)
			.Image(this, &SStatsTableRow::GetOutlineBrush, ColumnId)
			.ColorAndOpacity(this, &SStatsTableRow::GetOutlineColorAndOpacity)
		]

		+SOverlay::Slot()
		[
			SNew(SStatsTableCell, SharedThis(this))
			.Visibility(this, &SStatsTableRow::IsColumnVisible, ColumnId)
			.StatsNodePtr(StatsNodePtr)
			.ColumnId(ColumnId)
			.HighlightText(HighlightText)
			.IsNameColumn(ColumnId == FStatsViewColumnFactory::Get().Collection[0]->Id) // name column
			.OnSetHoveredTableCell(this, &SStatsTableRow::OnSetHoveredTableCell)
		];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SStatsTableRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return SMultiColumnTableRow<FStatsNodePtr>::OnDragDetected(MyGeometry, MouseEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<IToolTip> SStatsTableRow::GetRowToolTip() const
{
	return RowToolTip.ToSharedRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsTableRow::InvalidateContent()
{
	RowToolTip->InvalidateWidget();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SStatsTableRow::GetBackgroundColorAndOpacity() const
{
	return FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SStatsTableRow::GetBackgroundColorAndOpacity(double Time) const
{
	const FLinearColor Color =	Time > TimeUtils::Second      ? FLinearColor(0.3f, 0.0f, 0.0f, 1.0f) :
								Time > TimeUtils::Milisecond  ? FLinearColor(0.3f, 0.1f, 0.0f, 1.0f) :
								Time > TimeUtils::Microsecond ? FLinearColor(0.0f, 0.1f, 0.0f, 1.0f) :
								                                FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	return Color;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SStatsTableRow::GetOutlineColorAndOpacity() const
{
	const FLinearColor NoColor(0.0f, 0.0f, 0.0f, 0.0f);
	const bool bShouldBeHighlighted = StatsNodePtr->GetName() == HighlightedNodeName.Get();
	const FLinearColor OutlineColorAndOpacity = bShouldBeHighlighted ? FLinearColor(FColorList::SlateBlue) : NoColor;
	return OutlineColorAndOpacity;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* SStatsTableRow::GetOutlineBrush(const FName ColumnId) const
{
	EHorizontalAlignment Result = HAlign_Center;
	if (IsColumnVisibleDelegate.IsBound())
	{
		Result = GetColumnOutlineHAlignmentDelegate.Execute(ColumnId);
	}

	const FSlateBrush* Brush = nullptr;
	if (Result == HAlign_Left)
	{
		Brush = FEditorStyle::GetBrush("Profiler.EventGraph.Border.L");
	}
	else if(Result == HAlign_Right)
	{
		Brush = FEditorStyle::GetBrush("Profiler.EventGraph.Border.R");
	}
	else
	{
		Brush = FEditorStyle::GetBrush("Profiler.EventGraph.Border.TB");
	}
	return Brush;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool SStatsTableRow::HandleShouldBeEnabled() const
{
	bool bResult = false;

	if (StatsNodePtr->IsGroup())
	{
		bResult = true;
	}
	else
	{
		if (OnShouldBeEnabled.IsBound())
		{
			bResult = OnShouldBeEnabled.Execute(StatsNodePtr->GetId());
		}
	}

	return bResult;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SStatsTableRow::IsColumnVisible(const FName ColumnId) const
{
	EVisibility Result = EVisibility::Collapsed;

	if (IsColumnVisibleDelegate.IsBound())
	{
		Result = IsColumnVisibleDelegate.Execute(ColumnId) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	return Result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SStatsTableRow::OnSetHoveredTableCell(const FName InColumnId, const FStatsNodePtr InSamplePtr)
{
	SetHoveredTableCellDelegate.ExecuteIfBound(InColumnId, InSamplePtr);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
