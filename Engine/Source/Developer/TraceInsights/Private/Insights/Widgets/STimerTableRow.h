// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

// Insights
#include "Insights/ViewModels/TimerNodeHelper.h"

DECLARE_DELEGATE_RetVal_OneParam(bool, FShouldBeEnabledDelegate, const uint32 /*TimerId*/);
DECLARE_DELEGATE_RetVal_OneParam(bool, FIsColumnVisibleDelegate, const FName /*ColumnId*/);
DECLARE_DELEGATE_TwoParams(FSetHoveredTimerTableCell, const FName /*ColumnId*/, const FTimerNodePtr /*TimerNodePtr*/);
DECLARE_DELEGATE_RetVal_OneParam(EHorizontalAlignment, FGetColumnOutlineHAlignmentDelegate, const FName /*ColumnId*/);

/** Widget that represents a table row in the timers' tree control. Generates widgets for each column on demand. */
class STimerTableRow : public SMultiColumnTableRow<FTimerNodePtr>
{
public:
	SLATE_BEGIN_ARGS(STimerTableRow) {}
		SLATE_EVENT(FShouldBeEnabledDelegate, OnShouldBeEnabled)
		SLATE_EVENT(FIsColumnVisibleDelegate, OnIsColumnVisible)
		SLATE_EVENT(FSetHoveredTimerTableCell, OnSetHoveredTableCell)
		SLATE_EVENT(FGetColumnOutlineHAlignmentDelegate, OnGetColumnOutlineHAlignmentDelegate)
		SLATE_ATTRIBUTE(FText, HighlightText)
		SLATE_ATTRIBUTE(FName, HighlightedTimerName)
		SLATE_ARGUMENT(FTimerNodePtr, TimerNodePtr)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnId) override;

	/**
	 * Called when Slate detects that a widget started to be dragged.
	 * Usage:
	 * A widget can ask Slate to detect a drag.
	 * OnMouseDown() reply with FReply::Handled().DetectDrag(SharedThis(this)).
	 * Slate will either send an OnDragDetected() event or do nothing.
	 * If the user releases a mouse button or leaves the widget before
	 * a drag is triggered (maybe user started at the very edge) then no event will be
	 * sent.
	 *
	 * @param  InMyGeometry  Widget geometry
	 * @param  InMouseEvent  MouseMove that triggered the drag
	 *
	 */
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

protected:
	/**
	 * @return a text which describes this table row, refers to both groups and timers
	 */
	FText GetText() const;

	/**
	 * @return a font style which is used to draw this table row, refers to both groups and timers
	 */
	FSlateFontInfo GetFont() const;

	/**
	 * @return a color and opacity value used to draw this table row, refers to both groups and timers
	 */
	FSlateColor GetColorAndOpacity() const;

	FSlateColor GetBackgroundColorAndOpacity() const;
	FSlateColor GetBackgroundColorAndOpacity(double Time) const;
	FSlateColor GetOutlineColorAndOpacity() const;
	const FSlateBrush* GetOutlineBrush(const FName ColumnId) const;
	bool HandleShouldBeEnabled() const;
	EVisibility IsColumnVisible(const FName ColumnId) const;
	void OnSetHoveredTableCell(const FName ColumnId, const FTimerNodePtr SamplePtr);

protected:
	/** Data context for this table row. */
	FTimerNodePtr TimerNodePtr;

	FShouldBeEnabledDelegate OnShouldBeEnabled;
	FIsColumnVisibleDelegate IsColumnVisibleDelegate;
	FSetHoveredTimerTableCell SetHoveredTableCellDelegate;
	FGetColumnOutlineHAlignmentDelegate GetColumnOutlineHAlignmentDelegate;

	/** Text to be highlighted on timer name. */
	TAttribute<FText> HighlightText;

	/** Name of the timer that should be drawn as highlighted. */
	TAttribute<FName> HighlightedTimerName;
};
