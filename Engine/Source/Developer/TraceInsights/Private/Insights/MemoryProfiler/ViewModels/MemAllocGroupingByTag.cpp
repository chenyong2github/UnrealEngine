// Copyright Epic Games, Inc. All Rights Reserved.
#include "MemAllocGroupingByTag.h"
#include "MemAllocNode.h"

#define LOCTEXT_NAMESPACE "Insights::FMemAllocGroupingByTag"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FMemAllocGroupingByTag);
	
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocGroupingByTag::FMemAllocGroupingByTag(const TraceServices::IAllocationsProvider& InTagProvider)
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByTag_ShortName", "Tag"),
		LOCTEXT("Grouping_ByTag_TitleName", "By Tag"),
		LOCTEXT("Grouping_Tag_Desc", "Creates a tree based on Tag hierarchy."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
	, TagProvider(InTagProvider)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemAllocGroupingByTag::GroupNodes(const TArray<FTableTreeNodePtr>& Nodes,
                                        FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable,
                                        std::atomic<bool>& bCancelGrouping) const
{
	using namespace TraceServices;
	
	ParentGroup.ClearChildren();

	struct FTagParentRel
	{
		TagIdType Id;
		TagIdType Parent;
	};
	TMap<TagIdType,FTableTreeNodePtr> TagIdToNode;
	TArray<FTagParentRel> TagParentFixup;
	TSet<TagIdType> NonEmptyIds;

	// Create tag nodes
	TagProvider.EnumerateTags([&](const TCHAR* Name, const TCHAR* FullName, TagIdType Id, TagIdType ParentId)
	{
		const auto Node = MakeShared<FTableTreeNode>(FName(Name), InParentTable);
		TagIdToNode.Emplace(Id, Node);
		TagParentFixup.Emplace(FTagParentRel {Id, ParentId});
	});

	// Untagged node is used if we cant find the id.
	const auto UntaggedNode = TagIdToNode.Find(0);
	NonEmptyIds.Add(0);

	// Add nodes to correct tag group
	for (FTableTreeNodePtr NodePtr : Nodes)
	{
		if (bCancelGrouping)
		{
			return;
		}

		if (NodePtr->IsGroup())
		{
			ParentGroup.AddChildAndSetGroupPtr(NodePtr);
			continue;
		}

		const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(*NodePtr);
		const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
		if (Alloc)
		{
			const TagIdType TagId = Alloc->GetTagId();
			const auto TagNode = TagIdToNode.Find(TagId);
			if (TagNode)
			{
				(*TagNode)->AddChildAndSetGroupPtr(NodePtr);
				NonEmptyIds.Add(TagId);
			}
			else if (UntaggedNode)
			{
				(*UntaggedNode)->AddChildAndSetGroupPtr(NodePtr);
			}
		}
	}

	// Sort by parent id
	Algo::SortBy(TagParentFixup, &FTagParentRel::Parent);
	
	// Fixup parent relationships and filter out empty nodes
	for (auto TagParentRel : TagParentFixup)
	{
		if (bCancelGrouping)
		{
			return;
		}
		
		if (!NonEmptyIds.Contains(TagParentRel.Id))
		{
			continue;
		}
		
		if (TagParentRel.Parent == ~0)
		{
			// Add to parent directly
			ParentGroup.AddChildAndSetGroupPtr(TagIdToNode[TagParentRel.Id]);
		}
		else
		{
			// Try to find the parent node and add it as a child
			if (const auto ParentNode = TagIdToNode.Find(TagParentRel.Parent))
			{
				(*ParentNode)->AddChildAndSetGroupPtr(TagIdToNode[TagParentRel.Id]);
			}

			// Indicate that parent also is non-empty. Since the fixup array is sorted by parent id the root nodes
			// be processed last.
			NonEmptyIds.Add(TagParentRel.Parent);
		}
	}
}
	
////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE

}
