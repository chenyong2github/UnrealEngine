// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FilterCollection.h"
#include "Misc/TextFilter.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STreeView.h"

// Insights
#include "Insights/Table/ViewModels/Table.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"

class FMenuBuilder;

namespace Trace
{
	class IAnalysisSession;
	struct FTimelineEvent;
}

namespace Insights
{

class FTreeNodeSorting;
class FTreeNodeGrouping;

////////////////////////////////////////////////////////////////////////////////////////////////////

/** The filter collection - used for updating the list of tree nodes. */
typedef TFilterCollection<const FTableTreeNodePtr&> FTableTreeNodeFilterCollection;

/** The text based filter - used for updating the list of tree nodes. */
typedef TTextFilter<const FTableTreeNodePtr&> FTableTreeNodeTextFilter;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * A custom widget used to display the list of tree nodes.
 */
class STableTreeView : public SCompoundWidget
{
public:
	/** Default constructor. */
	STableTreeView();

	/** Virtual destructor. */
	virtual ~STableTreeView();

	SLATE_BEGIN_ARGS(STableTreeView) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<Insights::FTable> InTablePtr);

	TSharedPtr<Insights::FTable> GetTable() const { return Table; }

	/**
	 * Rebuilds the tree (if necessary).
	 * @param bResync - If true, it forces a resync even if the list did not changed since last sync.
	 */
	virtual void RebuildTree(bool bResync = true);

	const FTableTreeNodePtr* FindNode(uint64 Id) const { return TableTreeNodesIdMap.Find(Id); }

	void SelectNodeByNodeId(uint64 Id);
	void SelectNodeByTableRowIndex(int32 RowIndex);

protected:
	void UpdateTree();

	/** Called when the analysis session has changed. */
	void InsightsManager_OnSessionChanged();

	/**
	 * Populates OutSearchStrings with the strings that should be used in searching.
	 *
	 * @param GroupOrStatNodePtr - the group and stat node to get a text description from.
	 * @param OutSearchStrings   - an array of strings to use in searching.
	 *
	 */
	void HandleItemToStringArray(const FTableTreeNodePtr& GroupOrStatNodePtr, TArray<FString>& OutSearchStrings) const;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Context Menu

	TSharedPtr<SWidget> TreeView_GetMenuContent();
	void TreeView_BuildSortByMenu(FMenuBuilder& MenuBuilder);
	void TreeView_BuildViewColumnMenu(FMenuBuilder& MenuBuilder);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Columns' Header

	void InitializeAndShowHeaderColumns();
	void TreeViewHeaderRow_CreateColumnArgs(const FTableColumn& Column);
	TSharedRef<SWidget> TreeViewHeaderRow_GenerateColumnMenu(const FTableColumn& Column);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Misc

	void TreeView_Refresh();

	/**
	 * Called by STreeView to retrieves the children for the specified parent item.
	 * @param InParent    - The parent node to retrieve the children from.
	 * @param OutChildren - List of children for the parent node.
	 */
	void TreeView_OnGetChildren(FTableTreeNodePtr InParent, TArray<FTableTreeNodePtr>& OutChildren);

	/** Called by STreeView when selection has changed. */
	void TreeView_OnSelectionChanged(FTableTreeNodePtr SelectedItem, ESelectInfo::Type SelectInfo);

	/** Called by STreeView when a tree item is double clicked. */
	void TreeView_OnMouseButtonDoubleClick(FTableTreeNodePtr TreeNode);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Table Row

	/** Called by STreeView to generate a table row for the specified item. */
	TSharedRef<ITableRow> TreeView_OnGenerateRow(FTableTreeNodePtr TreeNode, const TSharedRef<STableViewBase>& OwnerTable);

	void TableRow_SetHoveredCell(TSharedPtr<FTable> TablePtr, TSharedPtr<FTableColumn> ColumnPtr, const FTableTreeNodePtr NodePtr);
	EHorizontalAlignment TableRow_GetColumnOutlineHAlignment(const FName ColumnId) const;

	FText TableRow_GetHighlightText() const;
	FName TableRow_GetHighlightedNodeName() const;

	bool TableRow_ShouldBeEnabled(const uint32 TreeNodeId) const;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Filtering

	/** Populates the group and stat tree with items based on the current data. */
	void ApplyFiltering();

	bool SearchBox_IsEnabled() const;
	void SearchBox_OnTextChanged(const FText& InFilterText);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Grouping

	void CreateGroupings();
	void CreateGroups();

	void GroupBy_OnSelectionChanged(TSharedPtr<FTreeNodeGrouping> NewGrouping, ESelectInfo::Type SelectInfo);

	TSharedRef<SWidget> GroupBy_OnGenerateWidget(TSharedPtr<FTreeNodeGrouping> InGrouping) const;

	FText GroupBy_GetSelectedText() const;

	FText GroupBy_GetSelectedTooltipText() const;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Sorting

	void CreateSortings();

	void UpdateCurrentSortingByColumn();
	void SortTreeNodes();

	EColumnSortMode::Type GetSortModeForColumn(const FName ColumnId) const;
	void SetSortModeForColumn(const FName& ColumnId, EColumnSortMode::Type SortMode);
	void OnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type SortMode);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Sorting actions

	// SortMode (HeaderMenu)
	bool HeaderMenu_SortMode_IsChecked(const FName ColumnId, const EColumnSortMode::Type InSortMode);
	bool HeaderMenu_SortMode_CanExecute(const FName ColumnId, const EColumnSortMode::Type InSortMode) const;
	void HeaderMenu_SortMode_Execute(const FName ColumnId, const EColumnSortMode::Type InSortMode);

	// SortMode (ContextMenu)
	bool ContextMenu_SortMode_IsChecked(const EColumnSortMode::Type InSortMode);
	bool ContextMenu_SortMode_CanExecute(const EColumnSortMode::Type InSortMode) const;
	void ContextMenu_SortMode_Execute(const EColumnSortMode::Type InSortMode);

	// SortByColumn (ContextMenu)
	bool ContextMenu_SortByColumn_IsChecked(const FName ColumnId);
	bool ContextMenu_SortByColumn_CanExecute(const FName ColumnId) const;
	void ContextMenu_SortByColumn_Execute(const FName ColumnId);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Column visibility actions

	// ShowColumn
	bool CanShowColumn(const FName ColumnId) const;
	void ShowColumn(const FName ColumnId);

	// HideColumn
	bool CanHideColumn(const FName ColumnId) const;
	void HideColumn(const FName ColumnId);

	// ToggleColumnVisibility
	bool IsColumnVisible(const FName ColumnId);
	bool CanToggleColumnVisibility(const FName ColumnId) const;
	void ToggleColumnVisibility(const FName ColumnId);

	// ShowAllColumns (ContextMenu)
	bool ContextMenu_ShowAllColumns_CanExecute() const;
	void ContextMenu_ShowAllColumns_Execute();

	// ResetColumns (ContextMenu)
	bool ContextMenu_ResetColumns_CanExecute() const;
	void ContextMenu_ResetColumns_Execute();

	////////////////////////////////////////////////////////////////////////////////////////////////////

protected:
	/** Table view model. */
	TSharedPtr<Insights::FTable> Table;

	/** A weak pointer to the profiler session used to populate this widget. */
	TSharedPtr<const Trace::IAnalysisSession>/*Weak*/ Session;

	//////////////////////////////////////////////////
	// Tree View, Columns

	/** The child STreeView widget. */
	TSharedPtr<STreeView<FTableTreeNodePtr>> TreeView;

	/** Column arguments used to initialize a new header column in the tree view, stored as column name to column arguments mapping. */
	TMap<FName, SHeaderRow::FColumn::FArguments> TreeViewHeaderColumnArgs;

	/** Holds the tree view header row widget which display all columns in the tree view. */
	TSharedPtr<SHeaderRow> TreeViewHeaderRow;

	/** External scrollbar used to synchronize tree view position. */
	TSharedPtr<SScrollBar> ExternalScrollbar;

	//////////////////////////////////////////////////
	// Hovered Column, Hovered Tree Node

	/** Name of the column currently being hovered by the mouse. */
	FName HoveredColumnId;

	/** A shared pointer to the tree node currently being hovered by the mouse. */
	FTableTreeNodePtr HoveredNodePtr;

	/** Name of the tree node that should be drawn as highlighted. */
	FName HighlightedNodeName;

	//////////////////////////////////////////////////
	// Tree Nodes

	/** All nodes. Each node corresponds to a table row. */
	TArray<FTableTreeNodePtr> TableTreeNodes;

	/** The array of group nodes. */
	TArray<FTableTreeNodePtr> GroupNodes;

	/** A filtered array of group and nodes to be displayed in the tree widget. */
	TArray<FTableTreeNodePtr> FilteredGroupNodes;

	/** All nodes, stored as Id -> FTableTreeNodePtr. */
	TMap<uint64, FTableTreeNodePtr> TableTreeNodesIdMap;

	/** Currently expanded group nodes. */
	TSet<FTableTreeNodePtr> ExpandedNodes;

	/** If true, the expanded nodes have been saved before applying a text filter. */
	bool bExpansionSaved;

	//////////////////////////////////////////////////
	// Search box and filters

	/** The search box widget used to filter items displayed in the stats and groups tree. */
	TSharedPtr<SSearchBox> SearchBox;

	/** The text based filter. */
	TSharedPtr<FTableTreeNodeTextFilter> TextFilter;

	/** The filter collection. */
	TSharedPtr<FTableTreeNodeFilterCollection> Filters;

	//////////////////////////////////////////////////
	// Grouping

	TArray<TSharedPtr<FTreeNodeGrouping>> AvailableGroupings;

	TSharedPtr<SComboBox<TSharedPtr<FTreeNodeGrouping>>> GroupByComboBox;

	/** How we group the tree nodes? */
	TSharedPtr<FTreeNodeGrouping> CurrentGrouping;

	//////////////////////////////////////////////////
	// Sorting

	/** All available sortings. */
	TArray<TSharedPtr<FTreeNodeSorting>> AvailableSortings;

	/** Sortings associated with a column. */
	TMap<FName, TSharedPtr<FTreeNodeSorting>> ColumnToSortingMap;

	/** Current sorting. It is nullptr if sorting is disabled. */
	TSharedPtr<FTreeNodeSorting> CurrentSorting;

	/** Name of the column currently being sorted. Can be NAME_None if sorting is disabled (CurrentSorting == nullptr) or if a complex sorting is used (CurrentSorting != nullptr). */
	FName ColumnBeingSorted;

	/** How we sort the nodes? Ascending or Descending. */
	EColumnSortMode::Type ColumnSortMode;

	static const FName DefaultColumnBeingSorted;
	static const EColumnSortMode::Type DefaultColumnSortMode;

	//////////////////////////////////////////////////

	double StatsStartTime;
	double StatsEndTime;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
