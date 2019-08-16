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
#include "Insights/ViewModels/StatsNode.h"
#include "Insights/ViewModels/StatsNodeHelper.h"
#include "Insights/ViewModels/StatsViewColumn.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMenuBuilder;

namespace Trace
{
	class IAnalysisSession;
	struct FTimelineEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename Type>
struct TAggregatedStatsEx
{
	static constexpr int32 HistogramLen = 100; // number of buckets per histogram

	TAggregatedStats<Type> BaseStats;

	// Histogram for computing median and lower/upper quartiles.
	int32 Histogram[HistogramLen];
	Type DT; // bucket size

	TAggregatedStatsEx()
	{
		FMemory::Memzero(Histogram, sizeof(int32) * HistogramLen);
		DT = Type(1);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/** The filter collection - used for updating the list of stats nodes. */
typedef TFilterCollection<const FStatsNodePtr&> FStatsNodeFilterCollection;

/** The text based filter - used for updating the list of stats nodes. */
typedef TTextFilter<const FStatsNodePtr&> FStatsNodeTextFilter;

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * A custom widget used to display the list of stats.
 */
class SStatsView : public SCompoundWidget
{
public:
	/** Default constructor. */
	SStatsView();

	/** Virtual destructor. */
	virtual ~SStatsView();

	SLATE_BEGIN_ARGS(SStatsView){}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 * @param InArgs - The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs);

	/**
	 * Rebuilds the tree (if necessary).
	 * @param bResync - If true, it forces a resync with list of stats counters from Analysis, even if the list did not changed since last sync.
	 */
	void RebuildTree(bool bResync = true);
	void UpdateStats(double StartTime, double EndTime);

	void SelectStatsNode(uint64 Id);

	//const TSet<FStatsNodePtr>& GetStatsNodes() const { return StatsNodes; }
	//const TMap<uint64, FStatsNodePtr> GetStatsNodesIdMap() const { return StatsNodesIdMap; }
	const FStatsNodePtr* GetStatsNode(uint64 Id) const { return StatsNodesIdMap.Find(Id); }

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
	void HandleItemToStringArray(const FStatsNodePtr& GroupOrStatNodePtr, TArray<FString>& OutSearchStrings) const;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Context Menu

	TSharedPtr<SWidget> TreeView_GetMenuContent();
	void TreeView_BuildSortByMenu(FMenuBuilder& MenuBuilder);
	void TreeView_BuildViewColumnMenu(FMenuBuilder& MenuBuilder);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Columns' Header

	void InitializeAndShowHeaderColumns();
	void TreeViewHeaderRow_CreateColumnArgs(const int32 ColumnIndex);
	void TreeViewHeaderRow_ShowColumn(const FName ColumnId);
	TSharedRef<SWidget> TreeViewHeaderRow_GenerateColumnMenu(const FStatsViewColumn& Column);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Misc

	void TreeView_Refresh();

	/**
	 * Called by STreeView to retrieves the children for the specified parent item.
	 * @param InParent    - The parent node to retrieve the children from.
	 * @param OutChildren - List of children for the parent node.
	 */
	void TreeView_OnGetChildren(FStatsNodePtr InParent, TArray<FStatsNodePtr>& OutChildren);

	/** Called by STreeView when selection has changed. */
	void TreeView_OnSelectionChanged(FStatsNodePtr SelectedItem, ESelectInfo::Type SelectInfo);

	/** Called by STreeView when a tree item is double clicked. */
	void TreeView_OnMouseButtonDoubleClick(FStatsNodePtr TreeNode);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Tree View - Table Row

	/** Called by STreeView to generate a table row for the specified item. */
	TSharedRef<ITableRow> TreeView_OnGenerateRow(FStatsNodePtr TreeNode, const TSharedRef<STableViewBase>& OwnerTable);

	bool TableRow_IsColumnVisible(const FName ColumnId) const;
	void TableRow_SetHoveredTableCell(const FName ColumnId, const FStatsNodePtr StatsNodePtr);
	EHorizontalAlignment TableRow_GetColumnOutlineHAlignment(const FName ColumnId) const;

	FText TableRow_GetHighlightText() const;
	FName TableRow_GetHighlightedStatsName() const;

	bool TableRow_ShouldBeEnabled(const uint32 StatsId) const;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Filtering

	/** Populates the group and stat tree with items based on the current data. */
	void ApplyFiltering();

	TSharedRef<SWidget> GetToggleButtonForStatsType(const EStatsNodeType StatType);
	void FilterByStatsType_OnCheckStateChanged(ECheckBoxState NewRadioState, const EStatsNodeType InStatType);
	ECheckBoxState FilterByStatsType_IsChecked(const EStatsNodeType InStatType) const;

	bool SearchBox_IsEnabled() const;
	void SearchBox_OnTextChanged(const FText& InFilterText);

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// GroupBy

	void CreateGroups();
	void CreateGroupByOptionsSources();

	void GroupBy_OnSelectionChanged(TSharedPtr<EStatsGroupingMode> NewGroupingMode, ESelectInfo::Type SelectInfo);

	TSharedRef<SWidget> GroupBy_OnGenerateWidget(TSharedPtr<EStatsGroupingMode> InGroupingMode) const;

	FText GroupBy_GetSelectedText() const;

	FText GroupBy_GetSelectedTooltipText() const;

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Sorting

	static const EColumnSortMode::Type GetDefaultColumnSortMode();
	static const FName GetDefaultColumnBeingSorted();

	void SortStats();

	EColumnSortMode::Type GetSortModeForColumn(const FName ColumnId) const;
	void SetSortModeForColumn(const FName& ColumnId, EColumnSortMode::Type SortMode);
	void OnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type SortMode);

	//void TreeView_BuildSortByMenu(FMenuBuilder& MenuBuilder);

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

	// HideColumn (HeaderMenu)
	bool HeaderMenu_HideColumn_CanExecute(const FName ColumnId) const;
	void HeaderMenu_HideColumn_Execute(const FName ColumnId);

	// ToggleColumn (ContextMenu)
	bool ContextMenu_ToggleColumn_IsChecked(const FName ColumnId);
	bool ContextMenu_ToggleColumn_CanExecute(const FName ColumnId) const;
	void ContextMenu_ToggleColumn_Execute(const FName ColumnId);

	// ShowAllColumns (ContextMenu)
	bool ContextMenu_ShowAllColumns_CanExecute() const;
	void ContextMenu_ShowAllColumns_Execute();

	// MinMaxMedColumns (ContextMenu)
	bool ContextMenu_ShowMinMaxMedColumns_CanExecute() const;
	void ContextMenu_ShowMinMaxMedColumns_Execute();

	// ResetColumns (ContextMenu)
	bool ContextMenu_ResetColumns_CanExecute() const;
	void ContextMenu_ResetColumns_Execute();

protected:
	/** A weak pointer to the profiler session used to populate this widget. */
	TSharedPtr<const Trace::IAnalysisSession>/*Weak*/ Session;

	//////////////////////////////////////////////////
	// Tree View, Columns

	/** The tree widget which holds the list of groups and stats corresponding with each group. */
	TSharedPtr<STreeView<FStatsNodePtr>> TreeView;

	/** Column metadata used to initialize column arguments, stored as PropertyName -> FEventGraphColumn. */
	TMap<FName, FStatsViewColumn> TreeViewHeaderColumns;

	/** Column arguments used to initialize a new header column in the tree view, stored as column name to column arguments mapping. */
	TMap<FName, SHeaderRow::FColumn::FArguments> TreeViewHeaderColumnArgs;

	/** Holds the tree view header row widget which display all columns in the tree view. */
	TSharedPtr<SHeaderRow> TreeViewHeaderRow;

	/** External scrollbar used to synchronize tree view position. */
	TSharedPtr<SScrollBar> ExternalScrollbar;

	//////////////////////////////////////////////////
	// Hovered Column, Hovered Stats Node

	/** Name of the column currently being hovered by the mouse. */
	FName HoveredColumnId;

	/** A shared pointer to the stats node currently being hovered by the mouse. */
	FStatsNodePtr HoveredStatsNodePtr;

	/** Name of the stats that should be drawn as highlighted. */
	FName HighlightedStatsName;

	//////////////////////////////////////////////////
	// Stats Nodes

	/** An array of group and stats nodes generated from the metadata. */
	TArray<FStatsNodePtr> GroupNodes;

	/** A filtered array of group and stats nodes to be displayed in the tree widget. */
	TArray<FStatsNodePtr> FilteredGroupNodes;

	/** All stats nodes. */
	TSet<FStatsNodePtr> StatsNodes;

	/** All stats nodes, stored as StatsName -> FStatsNodePtr. */
	//TMap<FName, FStatsNodePtr> StatsNodesMap;

	/** All stats nodes, stored as StatsId -> FStatsNodePtr. */
	TMap<uint64, FStatsNodePtr> StatsNodesIdMap;

	/** Currently expanded group nodes. */
	TSet<FStatsNodePtr> ExpandedNodes;

	/** If true, the expanded nodes have been saved before applying a text filter. */
	bool bExpansionSaved;

	//////////////////////////////////////////////////
	// Search box and filters

	/** The search box widget used to filter items displayed in the stats and groups tree. */
	TSharedPtr<SSearchBox> SearchBox;

	/** The text based filter. */
	TSharedPtr<FStatsNodeTextFilter> TextFilter;

	/** The filter collection. */
	TSharedPtr<FStatsNodeFilterCollection> Filters;

	/** Holds the visibility of each stats type. */
	bool bStatsNodeIsVisible[static_cast<int>(EStatsNodeType::InvalidOrMax)];

	//////////////////////////////////////////////////
	// Grouping

	TArray<TSharedPtr<EStatsGroupingMode>> GroupByOptionsSource;

	TSharedPtr<SComboBox<TSharedPtr<EStatsGroupingMode>>> GroupByComboBox;

	/** How we group the stats? */
	EStatsGroupingMode GroupingMode;

	//////////////////////////////////////////////////
	// Sorting

	/** How we sort the stats? */
	EColumnSortMode::Type ColumnSortMode;

	/** Name of the column currently being sorted, NAME_None if sorting is disabled. */
	FName ColumnBeingSorted;

	//////////////////////////////////////////////////

	double StatsStartTime;
	double StatsEndTime;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
