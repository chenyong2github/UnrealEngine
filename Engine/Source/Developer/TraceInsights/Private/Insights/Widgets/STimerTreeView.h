// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STreeView.h"

// Insights
#include "Insights/ViewModels/TimerNode.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMenuBuilder;


////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A custom widget used to display the timers in a tree view (ex. Callers and Callees).
 */
class STimerTreeView : public SCompoundWidget
{
public:
	/** Default constructor. */
	STimerTreeView();

	/** Virtual destructor. */
	virtual ~STimerTreeView();

	SLATE_BEGIN_ARGS(STimerTreeView) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, const FText& InViewName);

	void Reset();
	void SetTree(const Trace::FTimingProfilerButterflyNode& Root);

private:
	FTimerNodePtr CreateTimerNodeRec(const Trace::FTimingProfilerButterflyNode& Node);
	void ExpandNodesRec(FTimerNodePtr NodePtr, int32 Depth);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Context Menu

	TSharedPtr<SWidget> TreeView_GetMenuContent();

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Columns' Header

	void InitializeAndShowHeaderColumns();
	void TreeViewHeaderRow_CreateTimerNameColumnArgs();
	void TreeViewHeaderRow_CreateCountColumnArgs();
	void TreeViewHeaderRow_CreateInclusiveTimeColumnArgs();
	void TreeViewHeaderRow_CreateExclusiveTimeColumnArgs();

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Misc

	void TreeView_Refresh();

	/**
	 * Called by STreeView to retrieves the children for the specified parent item.
	 * @param InParent    - The parent node to retrieve the children from.
	 * @param OutChildren - List of children for the parent node.
	 */
	void TreeView_OnGetChildren(FTimerNodePtr InParent, TArray<FTimerNodePtr>& OutChildren);

	/** Called by STreeView when selection has changed. */
	void TreeView_OnSelectionChanged(FTimerNodePtr SelectedItem, ESelectInfo::Type SelectInfo);

	/** Called by STreeView when a tree item is double clicked. */
	void TreeView_OnMouseButtonDoubleClick(FTimerNodePtr TreeNode);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Table Row

	/** Called by STreeView to generate a table row for the specified item. */
	TSharedRef<ITableRow> TreeView_OnGenerateRow(FTimerNodePtr TreeNode, const TSharedRef<STableViewBase>& OwnerTable);

	bool TableRow_IsColumnVisible(const FName ColumnId) const;
	void TableRow_SetHoveredTableCell(const FName ColumnId, const FTimerNodePtr TimerNodePtr);
	EHorizontalAlignment TableRow_GetColumnOutlineHAlignment(const FName ColumnId) const;

	FText TableRow_GetHighlightText() const;
	FName TableRow_GetHighlightedNodeName() const;

	bool TableRow_ShouldBeEnabled(const uint32 TimerId) const;

	////////////////////////////////////////////////////////////////////////////////////////////////////

private:

	/** The view name (ex.: "Callers" or "Callees"). */
	FText ViewName;

	//////////////////////////////////////////////////
	// Tree View, Columns

	/** The tree widget which holds the list of groups and timers corresponding with each group. */
	TSharedPtr<STreeView<FTimerNodePtr>> TreeView;

	/** Column arguments used to initialize a new header column in the tree view, stored as column name to column arguments mapping. */
	TMap<FName, SHeaderRow::FColumn::FArguments> TreeViewHeaderColumnArgs;

	/** Holds the tree view header row widget which display all columns in the tree view. */
	TSharedPtr<SHeaderRow> TreeViewHeaderRow;

	/** External scrollbar used to synchronize tree view position. */
	TSharedPtr<SScrollBar> ExternalScrollbar;

	//////////////////////////////////////////////////
	// Hovered Column, Hovered Timer Node

	/** Name of the column currently being hovered by the mouse. */
	FName HoveredColumnId;

	/** A shared pointer to the timer node currently being hovered by the mouse. */
	FTimerNodePtr HoveredTimerNodePtr;

	/** Name of the timer that should be drawn as highlighted. */
	FName HighlightedNodeName;

	//////////////////////////////////////////////////
	// Timer Nodes

	/** The root node(s) of the tree. */
	TArray<FTimerNodePtr> TreeNodes;

	//////////////////////////////////////////////////

	double StatsStartTime;
	double StatsEndTime;
	uint32 StatsTimerId;

	static const FName TimerNameColumnId;
	static const FName CountColumnId;
	static const FName InclusiveTimeColumnId;
	static const FName ExclusiveTimeColumnId;
};
