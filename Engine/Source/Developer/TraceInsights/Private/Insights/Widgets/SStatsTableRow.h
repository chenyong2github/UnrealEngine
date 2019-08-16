// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

// Insights
#include "Insights/ViewModels/StatsNodeHelper.h"

DECLARE_DELEGATE_RetVal_OneParam(bool, FShouldBeEnabledDelegate, const uint32 /*StatsId*/);
DECLARE_DELEGATE_RetVal_OneParam(bool, FIsColumnVisibleDelegate, const FName /*ColumnId*/);
DECLARE_DELEGATE_TwoParams(FSetHoveredStatsTableCell, const FName /*ColumnId*/, const FStatsNodePtr /*StatsNodePtr*/);
DECLARE_DELEGATE_RetVal_OneParam(EHorizontalAlignment, FGetColumnOutlineHAlignmentDelegate, const FName /*ColumnId*/);

/** Widget that represents a table row in the stats' tree control. Generates widgets for each column on demand. */
class SStatsTableRow : public SMultiColumnTableRow<FStatsNodePtr>
{
public:
	SLATE_BEGIN_ARGS(SStatsTableRow) {}
		SLATE_EVENT(FShouldBeEnabledDelegate, OnShouldBeEnabled)
		SLATE_EVENT(FIsColumnVisibleDelegate, OnIsColumnVisible)
		SLATE_EVENT(FSetHoveredStatsTableCell, OnSetHoveredTableCell)
		SLATE_EVENT(FGetColumnOutlineHAlignmentDelegate, OnGetColumnOutlineHAlignmentDelegate)
		SLATE_ATTRIBUTE(FText, HighlightText)
		SLATE_ATTRIBUTE(FName, HighlightedStatsName)
		SLATE_ARGUMENT(FStatsNodePtr, StatsNodePtr)
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
	 * @return a text which describes this table row, refers to both groups and stats
	 */
	FText GetText() const;

	/**
	 * @return a font style which is used to draw this table row, refers to both groups and stats
	 */
	FSlateFontInfo GetFont() const;

	/**
	 * @return a color and opacity value used to draw this table row, refers to both groups and stats
	 */
	FSlateColor GetColorAndOpacity() const;

	FSlateColor GetBackgroundColorAndOpacity() const;
	FSlateColor GetBackgroundColorAndOpacity(double Time) const;
	FSlateColor GetOutlineColorAndOpacity() const;
	const FSlateBrush* GetOutlineBrush(const FName ColumnId) const;
	bool HandleShouldBeEnabled() const;
	EVisibility IsColumnVisible(const FName ColumnId) const;
	void OnSetHoveredTableCell(const FName ColumnId, const FStatsNodePtr SamplePtr);

protected:
	/** Data context for this table row. */
	FStatsNodePtr StatsNodePtr;

	FShouldBeEnabledDelegate OnShouldBeEnabled;
	FIsColumnVisibleDelegate IsColumnVisibleDelegate;
	FSetHoveredStatsTableCell SetHoveredTableCellDelegate;
	FGetColumnOutlineHAlignmentDelegate GetColumnOutlineHAlignmentDelegate;

	/** Text to be highlighted on stats name. */
	TAttribute<FText> HighlightText;

	/** Name of the stats that should be drawn as highlighted. */
	TAttribute<FName> HighlightedStatsName;
};
