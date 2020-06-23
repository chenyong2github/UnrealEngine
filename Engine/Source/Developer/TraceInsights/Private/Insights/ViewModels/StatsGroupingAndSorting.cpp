// Copyright Epic Games, Inc. All Rights Reserved.

#include "StatsGroupingAndSorting.h"

#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/ViewModels/StatsNodeHelper.h"

#define LOCTEXT_NAMESPACE "StatsNode"

// Sort by name (ascending).
#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetName().LexicalLess(B->GetName());
//#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetDefaultSortOrder() < B->GetDefaultSortOrder();

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting by Stats Type
////////////////////////////////////////////////////////////////////////////////////////////////////

FStatsNodeSortingByStatsType::FStatsNodeSortingByStatsType(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByStatsType")),
		LOCTEXT("Sorting_ByStatsType_Name", "By Type"),
		LOCTEXT("Sorting_ByStatsType_Title", "Sort By Type"),
		LOCTEXT("Sorting_ByStatsType_Desc", "Sort by stats counter type."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNodeSortingByStatsType::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FStatsNode::TypeName);
			const FStatsNodePtr StatsNodeA = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(A);

			ensure(B.IsValid() && B->GetTypeName() == FStatsNode::TypeName);
			const FStatsNodePtr StatsNodeB = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(B);

			if (StatsNodeA->GetType() == StatsNodeB->GetType())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by timer type (ascending).
				return StatsNodeA->GetType() < StatsNodeB->GetType();
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FStatsNode::TypeName);
			const FStatsNodePtr StatsNodeA = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(A);

			ensure(B.IsValid() && B->GetTypeName() == FStatsNode::TypeName);
			const FStatsNodePtr StatsNodeB = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(B);

			if (StatsNodeA->GetType() == StatsNodeB->GetType())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by timer type (descending).
				return StatsNodeB->GetType() < StatsNodeA->GetType();
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sort by Count (Aggregated Statistic)
////////////////////////////////////////////////////////////////////////////////////////////////////

FStatsNodeSortingByCount::FStatsNodeSortingByCount(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByCount")),
		LOCTEXT("Sorting_ByCount_Name", "By Count"),
		LOCTEXT("Sorting_ByCount_Title", "Sort By Count"),
		LOCTEXT("Sorting_ByCount_Desc", "Sort by aggregated value count."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStatsNodeSortingByCount::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FStatsNode::TypeName);
			const FStatsNodePtr StatsNodeA = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(A);
			const uint64 ValueA = StatsNodeA->GetAggregatedStats().Count;

			ensure(B.IsValid() && B->GetTypeName() == FStatsNode::TypeName);
			const FStatsNodePtr StatsNodeB = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(B);
			const uint64 ValueB = StatsNodeB->GetAggregatedStats().Count;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by instance count (ascending).
				return ValueA < ValueB;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FStatsNode::TypeName);
			const FStatsNodePtr StatsNodeA = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(A);
			const uint64 ValueA = StatsNodeA->GetAggregatedStats().Count;

			ensure(B.IsValid() && B->GetTypeName() == FStatsNode::TypeName);
			const FStatsNodePtr StatsNodeB = StaticCastSharedPtr<FStatsNode, Insights::FBaseTreeNode>(B);
			const uint64 ValueB = StatsNodeB->GetAggregatedStats().Count;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by instance count (descending).
				return ValueB < ValueA;
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef INSIGHTS_DEFAULT_SORTING_NODES
#undef LOCTEXT_NAMESPACE
