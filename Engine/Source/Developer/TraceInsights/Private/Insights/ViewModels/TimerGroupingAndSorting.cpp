// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimerGroupingAndSorting.h"

#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/ViewModels/TimerNodeHelper.h"

#define LOCTEXT_NAMESPACE "TimerNode"

// Sort by name (ascending).
#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetName().LexicalLess(B->GetName());
//#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetDefaultSortOrder() < B->GetDefaultSortOrder();

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting by Timer Type
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimerNodeSortingByTimerType::FTimerNodeSortingByTimerType(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByTimerType")),
		LOCTEXT("Sorting_ByTimerType_Name", "By Type"),
		LOCTEXT("Sorting_ByTimerType_Title", "Sort By Type"),
		LOCTEXT("Sorting_ByTimerType_Desc", "Sort by timer type."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerNodeSortingByTimerType::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeA = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(A);

			ensure(B.IsValid() && B->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeB = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(B);

			if (TimerNodeA->GetType() == TimerNodeB->GetType())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by timer type (ascending).
				return TimerNodeA->GetType() < TimerNodeB->GetType();
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeA = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(A);

			ensure(B.IsValid() && B->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeB = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(B);

			if (TimerNodeA->GetType() == TimerNodeB->GetType())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by timer type (descending).
				return TimerNodeB->GetType() < TimerNodeA->GetType();
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sort by Instance Count
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimerNodeSortingByInstanceCount::FTimerNodeSortingByInstanceCount(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByInstanceCount")),
		LOCTEXT("Sorting_ByInstanceCount_Name", "By Instance Count"),
		LOCTEXT("Sorting_ByInstanceCount_Title", "Sort By Instance Count"),
		LOCTEXT("Sorting_ByInstanceCount_Desc", "Sort by aggregated instance count."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerNodeSortingByInstanceCount::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeA = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(A);
			const uint64 ValueA = TimerNodeA->GetAggregatedStats().InstanceCount;

			ensure(B.IsValid() && B->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeB = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(B);
			const uint64 ValueB = TimerNodeB->GetAggregatedStats().InstanceCount;

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
			ensure(A.IsValid() && A->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeA = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(A);
			const uint64 ValueA = TimerNodeA->GetAggregatedStats().InstanceCount;

			ensure(B.IsValid() && B->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeB = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(B);
			const uint64 ValueB = TimerNodeB->GetAggregatedStats().InstanceCount;

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
// Sort by Total Inclusive Time
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimerNodeSortingByTotalInclusiveTime::FTimerNodeSortingByTotalInclusiveTime(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByTotalInclusiveTime")),
		LOCTEXT("Sorting_ByTotalInclusiveTime_Name", "By Total Inclusive Time"),
		LOCTEXT("Sorting_ByTotalInclusiveTime_Title", "Sort By Total Inclusive Time"),
		LOCTEXT("Sorting_ByTotalInclusiveTime_Desc", "Sort by aggregated total inclusive time."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerNodeSortingByTotalInclusiveTime::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeA = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(A);
			const double ValueA = TimerNodeA->GetAggregatedStats().TotalInclusiveTime;

			ensure(B.IsValid() && B->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeB = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(B);
			const double ValueB = TimerNodeB->GetAggregatedStats().TotalInclusiveTime;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by total inclusive time (ascending).
				return ValueA < ValueB;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeA = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(A);
			const double ValueA = TimerNodeA->GetAggregatedStats().TotalInclusiveTime;

			ensure(B.IsValid() && B->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeB = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(B);
			const double ValueB = TimerNodeB->GetAggregatedStats().TotalInclusiveTime;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by total inclusive time (descending).
				return ValueB < ValueA;
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sort by Total Exclusive Time
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimerNodeSortingByTotalExclusiveTime::FTimerNodeSortingByTotalExclusiveTime(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByTotalExclusiveTime")),
		LOCTEXT("Sorting_ByTotalExclusiveTime_Name", "By Total Exclusive Time"),
		LOCTEXT("Sorting_ByTotalExclusiveTime_Title", "Sort By Total Exclusive Time"),
		LOCTEXT("Sorting_ByTotalExclusiveTime_Desc", "Sort by aggregated total exclusive time."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimerNodeSortingByTotalExclusiveTime::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeA = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(A);
			const double ValueA = TimerNodeA->GetAggregatedStats().TotalExclusiveTime;

			ensure(B.IsValid() && B->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeB = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(B);
			const double ValueB = TimerNodeB->GetAggregatedStats().TotalExclusiveTime;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by total exclusive time (ascending).
				return ValueA < ValueB;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeA = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(A);
			const double ValueA = TimerNodeA->GetAggregatedStats().TotalExclusiveTime;

			ensure(B.IsValid() && B->GetTypeName() == FTimerNode::TypeName);
			const FTimerNodePtr TimerNodeB = StaticCastSharedPtr<FTimerNode, Insights::FBaseTreeNode>(B);
			const double ValueB = TimerNodeB->GetAggregatedStats().TotalExclusiveTime;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by total exclusive time (descending).
				return ValueB < ValueA;
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef INSIGHTS_DEFAULT_SORTING_NODES
#undef LOCTEXT_NAMESPACE
