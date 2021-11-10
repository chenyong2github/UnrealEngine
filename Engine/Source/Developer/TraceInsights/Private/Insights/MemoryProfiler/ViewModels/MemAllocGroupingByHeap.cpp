// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocGroupingByHeap.h"
#include "CallstackFormatting.h"
#include "Internationalization/Internationalization.h"

// Insights
#include "Insights/MemoryProfiler/ViewModels/MemAllocNode.h"

#define LOCTEXT_NAMESPACE "Insights::FMemAllocGroupingByHeap"

namespace Insights
{
	
INSIGHTS_IMPLEMENT_RTTI(FMemAllocGroupingByHeap)
	
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocGroupingByHeap::FMemAllocGroupingByHeap(const TraceServices::IAllocationsProvider& InAllocProvider)
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByHeap_ShortName", "Heap"),
		LOCTEXT("Grouping_ByHeap_TitleName", "By Heap"),
		LOCTEXT("Grouping_Heap_Desc", "Creates a tree based on heap."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
	, AllocProvider(InAllocProvider)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocGroupingByHeap::~FMemAllocGroupingByHeap()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTableTreeNodePtr MakeGroupNodeHierarchy(const TraceServices::IAllocationsProvider::FHeapSpec& Spec, TWeakPtr<FTable>& InParentTable, TArray<FTableTreeNodePtr>& NodeTable)
{
	auto Node = MakeShared<FTableTreeNode>(FName(Spec.Name), InParentTable);
	NodeTable[Spec.Id] = Node;
	for (TraceServices::IAllocationsProvider::FHeapSpec* ChildSpec : Spec.Children)
	{
		auto ChildNode = MakeGroupNodeHierarchy(*ChildSpec, InParentTable, NodeTable);
		Node->AddChildAndSetGroupPtr(ChildNode);
	}
	return Node;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemAllocGroupingByHeap::GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup,
	TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const
{
	ParentGroup.ClearChildren();

	// Build heap hierarchy
	TArray<FTableTreeNodePtr> HeapNodes;
	HeapNodes.AddZeroed(256);
	
	AllocProvider.EnumerateRootHeaps([&](uint8 Id, const TraceServices::IAllocationsProvider::FHeapSpec& Spec)
	{
		ParentGroup.AddChildAndSetGroupPtr(MakeGroupNodeHierarchy(Spec, InParentTable, HeapNodes));
	});

	// Add allocations nodes to heaps
	for (FTableTreeNodePtr NodePtr : Nodes)
	{
		if (bCancelGrouping)
		{
			return;
		}
		
		const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(*NodePtr);
		const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();

		if (Alloc)
		{
			//todo: Calculating the real HeapId when the allocation is first added to the provider was too expensive, so deferring that operation to here could make sense.
			//const uint8 HeapId = AllocProvider.GetParentBlock(Alloc->GetAddress());
			const uint8 HeapId = Alloc->GetRootHeap();
			if (FTableTreeNodePtr GroupPtr = HeapNodes[HeapId])
			{
				GroupPtr->AddChildAndSetGroupPtr(NodePtr);
			}
		}
	}
}
	
////////////////////////////////////////////////////////////////////////////////////////////////////
} // namespace Insights

#undef LOCTEXT_NAMESPACE