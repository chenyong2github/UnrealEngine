// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetEventGroupingAndSorting.h"

#include "Insights/Table/ViewModels/TableColumn.h"
#include "Insights/NetworkingProfiler/ViewModels/NetEventNodeHelper.h"

#define LOCTEXT_NAMESPACE "NetEventNode"

// Sort by name (ascending).
#define INSIGHTS_DEFAULT_SORTING_NODES(A, B) return A->GetName().LexicalLess(B->GetName());

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sorting by Event Type
////////////////////////////////////////////////////////////////////////////////////////////////////

FNetEventNodeSortingByEventType::FNetEventNodeSortingByEventType(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByEventType")),
		LOCTEXT("Sorting_ByEventType_Name", "By Type"),
		LOCTEXT("Sorting_ByEventType_Title", "Sort By Type"),
		LOCTEXT("Sorting_ByEventType_Desc", "Sort by event type (ascending or descending), then by name (ascending)."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetEventNodeSortingByEventType::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeA = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(A);

			ensure(B.IsValid() && B->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeB = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(B);

			if (NetEventNodeA->GetType() == NetEventNodeB->GetType())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by net event type (ascending).
				return NetEventNodeA->GetType() < NetEventNodeB->GetType();
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeA = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(A);

			ensure(B.IsValid() && B->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeB = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(B);

			if (NetEventNodeA->GetType() == NetEventNodeB->GetType())
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by net event type (descending).
				return NetEventNodeB->GetType() < NetEventNodeA->GetType();
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sort by Instance Count
////////////////////////////////////////////////////////////////////////////////////////////////////

FNetEventNodeSortingByInstanceCount::FNetEventNodeSortingByInstanceCount(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByInstanceCount")),
		LOCTEXT("Sorting_ByInstanceCount_Name", "By Instance Count"),
		LOCTEXT("Sorting_ByInstanceCount_Title", "Sort By Instance Count"),
		LOCTEXT("Sorting_ByInstanceCount_Desc", "Sort by aggregated instance count (ascending or descending), then by name (ascending)."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetEventNodeSortingByInstanceCount::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeA = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(A);
			const uint64 ValueA = NetEventNodeA->GetAggregatedStats().InstanceCount;

			ensure(B.IsValid() && B->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeB = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(B);
			const uint64 ValueB = NetEventNodeB->GetAggregatedStats().InstanceCount;

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
			ensure(A.IsValid() && A->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeA = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(A);
			const uint64 ValueA = NetEventNodeA->GetAggregatedStats().InstanceCount;

			ensure(B.IsValid() && B->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeB = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(B);
			const uint64 ValueB = NetEventNodeB->GetAggregatedStats().InstanceCount;

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
// Sort by Total Inclusive Size
////////////////////////////////////////////////////////////////////////////////////////////////////

FNetEventNodeSortingByTotalInclusiveSize::FNetEventNodeSortingByTotalInclusiveSize(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByTotalInclusiveSize")),
		LOCTEXT("Sorting_ByTotalInclusiveSize_Name", "By Total Inclusive Size"),
		LOCTEXT("Sorting_ByTotalInclusiveSize_Title", "Sort By Total Inclusive Size"),
		LOCTEXT("Sorting_ByTotalInclusiveSize_Desc", "Sort by aggregated total inclusive size (ascending or descending), then by name (ascending)."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetEventNodeSortingByTotalInclusiveSize::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeA = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(A);
			const uint32 ValueA = NetEventNodeA->GetAggregatedStats().TotalInclusive;

			ensure(B.IsValid() && B->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeB = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(B);
			const uint32 ValueB = NetEventNodeB->GetAggregatedStats().TotalInclusive;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by total inclusive size (ascending).
				return ValueA < ValueB;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeA = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(A);
			const uint32 ValueA = NetEventNodeA->GetAggregatedStats().TotalInclusive;

			ensure(B.IsValid() && B->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeB = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(B);
			const uint32 ValueB = NetEventNodeB->GetAggregatedStats().TotalInclusive;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by total inclusive size (descending).
				return ValueB < ValueA;
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sort by Total Exclusive Size
////////////////////////////////////////////////////////////////////////////////////////////////////

FNetEventNodeSortingByTotalExclusiveSize::FNetEventNodeSortingByTotalExclusiveSize(TSharedRef<Insights::FTableColumn> InColumnRef)
	: Insights::FTableCellValueSorter(
		FName(TEXT("ByTotalExclusiveSize")),
		LOCTEXT("Sorting_ByTotalExclusiveSize_Name", "By Total Exclusive Size"),
		LOCTEXT("Sorting_ByTotalExclusiveSize_Title", "Sort By Total Exclusive Size"),
		LOCTEXT("Sorting_ByTotalExclusiveSize_Desc", "Sort by aggregated total exclusive size (ascending or descending), then by name (ascending)."),
		InColumnRef)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FNetEventNodeSortingByTotalExclusiveSize::Sort(TArray<Insights::FBaseTreeNodePtr>& NodesToSort, Insights::ESortMode SortMode) const
{
	if (SortMode == Insights::ESortMode::Ascending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeA = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(A);
			const uint32 ValueA = NetEventNodeA->GetAggregatedStats().TotalExclusive;

			ensure(B.IsValid() && B->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeB = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(B);
			const uint32 ValueB = NetEventNodeB->GetAggregatedStats().TotalExclusive;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by total exclusive size (ascending).
				return ValueA < ValueB;
			}
		});
	}
	else // if (SortMode == ESortMode::Descending)
	{
		NodesToSort.Sort([](const Insights::FBaseTreeNodePtr& A, const Insights::FBaseTreeNodePtr& B) -> bool
		{
			ensure(A.IsValid() && A->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeA = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(A);
			const uint32 ValueA = NetEventNodeA->GetAggregatedStats().TotalExclusive;

			ensure(B.IsValid() && B->GetTypeName() == FNetEventNode::TypeName);
			const FNetEventNodePtr NetEventNodeB = StaticCastSharedPtr<FNetEventNode, Insights::FBaseTreeNode>(B);
			const uint32 ValueB = NetEventNodeB->GetAggregatedStats().TotalExclusive;

			if (ValueA == ValueB)
			{
				INSIGHTS_DEFAULT_SORTING_NODES(A, B)
			}
			else
			{
				// Sort by total exclusive size (descending).
				return ValueB < ValueA;
			}
		});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Grouping by Event Type
////////////////////////////////////////////////////////////////////////////////////////////////////

FNetEventNodeGroupingByEventType::FNetEventNodeGroupingByEventType()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByEventType_ShortName", "Type"),
		LOCTEXT("Grouping_ByEventType_TitleName", "Type"),
		LOCTEXT("Grouping_ByEventType_Desc", "Creates a group for each net event type."),
		TEXT("Profiler.FiltersAndPresets.StatTypeIcon"), //TODO
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Insights::FTreeNodeGroupInfo FNetEventNodeGroupingByEventType::GetGroupForNode(const Insights::FBaseTreeNodePtr InNodePtr) const
{
	if (InNodePtr->GetTypeName() == FNetEventNode::TypeName)
	{
		const FNetEventNodePtr& NetEventNodePtr = StaticCastSharedPtr<FNetEventNode>(InNodePtr);
		return { FName(*NetEventNodeTypeHelper::ToText(NetEventNodePtr->GetType()).ToString()), true };
	}
	else
	{
		return { FName("Unknown"), true };
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Grouping by Level
////////////////////////////////////////////////////////////////////////////////////////////////////

FNetEventNodeGroupingByLevel::FNetEventNodeGroupingByLevel()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByLevel_ShortName", "Level"),
		LOCTEXT("Grouping_ByLevel_TitleName", "Level"),
		LOCTEXT("Grouping_ByLevel_Desc", "Creates a group for each level."),
		TEXT("Profiler.FiltersAndPresets.StatTypeIcon"), //TODO
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Insights::FTreeNodeGroupInfo FNetEventNodeGroupingByLevel::GetGroupForNode(const Insights::FBaseTreeNodePtr InNodePtr) const
{
	if (InNodePtr->GetTypeName() == FNetEventNode::TypeName)
	{
		const FNetEventNodePtr& NetEventNodePtr = StaticCastSharedPtr<FNetEventNode>(InNodePtr);
		return { FName(*FString::Printf(TEXT("Level %d"), NetEventNodePtr->GetLevel())), true };
	}
	else
	{
		return { FName("Unknown"), true };
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef INSIGHTS_DEFAULT_SORTING_NODES
#undef LOCTEXT_NAMESPACE
