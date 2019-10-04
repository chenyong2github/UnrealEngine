// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/TimerNode.h"
#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"
#include "Insights/Table/ViewModels/TreeNodeGrouping.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorters
////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerNodeSortingByTimerType: public Insights::FTableCellValueSorter
{
public:
	FTimerNodeSortingByTimerType(TSharedRef<Insights::FTableColumn> InColumnRef);

	virtual void Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerNodeSortingByInstanceCount : public Insights::FTableCellValueSorter
{
public:
	FTimerNodeSortingByInstanceCount(TSharedRef<Insights::FTableColumn> InColumnRef);

	virtual void Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerNodeSortingByTotalInclusiveTime : public Insights::FTableCellValueSorter
{
public:
	FTimerNodeSortingByTotalInclusiveTime(TSharedRef<Insights::FTableColumn> InColumnRef);

	virtual void Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimerNodeSortingByTotalExclusiveTime : public Insights::FTableCellValueSorter
{
public:
	FTimerNodeSortingByTotalExclusiveTime(TSharedRef<Insights::FTableColumn> InColumnRef);

	virtual void Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Organizers
////////////////////////////////////////////////////////////////////////////////////////////////////

/** Enumerates types of grouping or sorting for the timer nodes. */
enum class ETimerGroupingMode
{
	/** Creates a single group for all timers. */
	Flat,

	/** Creates one group for one letter. */
	ByName,

	/** Creates groups based on timer metadata group names. */
	ByMetaGroupName,

	/** Creates one group for each timer type. */
	ByType,

	ByTotalInclusiveTime,

	ByTotalExclusiveTime,

	ByInstanceCount,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

/** Type definition for shared pointers to instances of ETimerGroupingMode. */
typedef TSharedPtr<ETimerGroupingMode> ETimerGroupingModePtr;

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Creates a group for each timer type. */
class FTimerNodeGroupingByTimerType : public Insights::FTreeNodeGrouping
{
public:
	FTimerNodeGroupingByTimerType();
	virtual ~FTimerNodeGroupingByTimerType() {}

	virtual Insights::FTreeNodeGroupInfo GetGroupForNode(const Insights::FBaseTreeNodePtr InNode) const override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
