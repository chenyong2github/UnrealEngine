// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocGroupingByCallstack.h"
#include "TraceServices/Model/Callstack.h"

// Insights
#include "Insights/MemoryProfiler/ViewModels/MemAllocNode.h"

#define LOCTEXT_NAMESPACE "Insights::FMemAllocGroupingByCallstack"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMemAllocGroupingByCallstack
////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FMemAllocGroupingByCallstack)

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocGroupingByCallstack::FMemAllocGroupingByCallstack(bool bInIsInverted, bool bInIsGroupingByFunction)
	: FTreeNodeGrouping(
		bInIsInverted ? LOCTEXT("Grouping_ByCallstack2_ShortName", "Inverted Callstack")
					  : LOCTEXT("Grouping_ByCallstack1_ShortName", "Callstack"),
		bInIsInverted ? LOCTEXT("Grouping_ByCallstack2_TitleName", "By Inverted Callstack")
					  : LOCTEXT("Grouping_ByCallstack1_TitleName", "By Callstack"),
		LOCTEXT("Grouping_Callstack_Desc", "Creates a tree based on callstack of each allocation."),
		TEXT("Profiler.FiltersAndPresets.GroupNameIcon"),
		nullptr)
	, bIsInverted(bInIsInverted)
	, bIsGroupingByFunction(bInIsGroupingByFunction)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocGroupingByCallstack::~FMemAllocGroupingByCallstack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemAllocGroupingByCallstack::GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const
{
	ParentGroup.ClearChildren();

	struct FCallstackGroup
	{
		FTableTreeNode* Node = nullptr;
		const TraceServices::FStackFrame* Frame = nullptr;
		TMap<uint64, FCallstackGroup*> GroupMap; // Callstack Frame Address -> FCallstackGroup*
		TMap<FName, FCallstackGroup*> GroupMapByName; // Group Name --> FCallstackGroup*
	};
	TArray<FCallstackGroup*> CallstackGroups;

	FCallstackGroup* Root = new FCallstackGroup();
	Root->Node = &ParentGroup;
	CallstackGroups.Add(Root);

	FTableTreeNode* UnsetGroupPtr = nullptr;
	TMap<const TraceServices::FCallstack*, FCallstackGroup*> GroupMapByCallstack; // Callstack* -> FCallstackGroup*

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

		FCallstackGroup* GroupPtr = Root;

		const FMemAllocNode& MemAllocNode = static_cast<const FMemAllocNode&>(*NodePtr);
		const FMemoryAlloc* Alloc = MemAllocNode.GetMemAlloc();
		if (Alloc)
		{
			const TraceServices::FCallstack* Callstack = Alloc->GetCallstack();
			if (Callstack)
			{
				const int32 NumFrames = static_cast<int32>(Callstack->Num());
				for (int32 FrameDepth = NumFrames - 1; FrameDepth >= 0; --FrameDepth)
				{
					const TraceServices::FStackFrame* Frame = Callstack->Frame(bIsInverted ? NumFrames - FrameDepth - 1 : FrameDepth);
					check(Frame != nullptr);

					if (bIsGroupingByFunction)
					{
						const FName GroupName = GetGroupName(Frame);
						FCallstackGroup** GroupPtrPtr = GroupPtr->GroupMapByName.Find(GroupName);
						if (!GroupPtrPtr)
						{
							FCallstackGroup* NewGroupPtr = new FCallstackGroup();
							CallstackGroups.Add(NewGroupPtr);

							NewGroupPtr->Node = CreateGroup(GroupName, InParentTable, *(GroupPtr->Node));
							NewGroupPtr->Node->SetTooltip(GetGroupTooltip(Frame));
							
							GroupPtr->GroupMapByName.Add(GroupName, NewGroupPtr);
							GroupPtr = NewGroupPtr;
						}
						else
						{
							GroupPtr = *GroupPtrPtr;
						}
					}
					else
					{
						FCallstackGroup** GroupPtrPtr = GroupPtr->GroupMap.Find(Frame->Addr);
						if (!GroupPtrPtr)
						{
							const FName GroupName = GetGroupName(Frame);

							FCallstackGroup* NewGroupPtr = new FCallstackGroup();
							CallstackGroups.Add(NewGroupPtr);

							NewGroupPtr->Node = CreateGroup(GroupName, InParentTable, *(GroupPtr->Node));
							NewGroupPtr->Node->SetTooltip(GetGroupTooltip(Frame));

							GroupPtr->GroupMap.Add(Frame->Addr, NewGroupPtr);
							GroupPtr = NewGroupPtr;
						}
						else
						{
							GroupPtr = *GroupPtrPtr;
						}
					}
				}
			}
		}

		if (GroupPtr != Root)
		{
			GroupPtr->Node->AddChildAndSetGroupPtr(NodePtr);
		}
		else
		{
			if (!UnsetGroupPtr)
			{
				UnsetGroupPtr = CreateUnsetGroup(InParentTable, ParentGroup);
			}
			UnsetGroupPtr->AddChildAndSetGroupPtr(NodePtr);
		}
	}

	for (FCallstackGroup* Group : CallstackGroups)
	{
		delete Group;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName FMemAllocGroupingByCallstack::GetGroupName(const TraceServices::FStackFrame* Frame) const
{
	const TraceServices::ESymbolQueryResult Result = Frame->Symbol->GetResult();
	if (Result == TraceServices::ESymbolQueryResult::OK)
	{
		return FName(Frame->Symbol->Name);
	}
	else if (Result == TraceServices::ESymbolQueryResult::Pending)
	{
		return FName(*FString::Printf(TEXT("0x%X [...]"), Frame->Addr));
	}
	else
	{
		return FName(*FString::Printf(TEXT("0x%X"), Frame->Addr));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FMemAllocGroupingByCallstack::GetGroupTooltip(const TraceServices::FStackFrame* Frame) const
{
	const TraceServices::ESymbolQueryResult Result = Frame->Symbol->GetResult();
	if (Result == TraceServices::ESymbolQueryResult::OK)
	{
		return FText::Format(LOCTEXT("CallstackFrameTooltipFmt1", "Callstack Frame\n\t{0}\n\t{1}\n\t{2}"),
			FText::FromString(FString::Printf(TEXT("0x%X"), Frame->Addr)),
			FText::FromString(FString(Frame->Symbol->Name)),
			FText::FromString(FString(Frame->Symbol->FileAndLine)));
	}
	else
	{
		return FText::Format(LOCTEXT("CallstackFrameTooltipFmt2", "Callstack Frame\n\t{0}\n\t{1}"),
			FText::FromString(FString::Printf(TEXT("0x%X"), Frame->Addr)),
			FText::FromString(FString(TraceServices::QueryResultToString(Result))));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTableTreeNode* FMemAllocGroupingByCallstack::CreateGroup(const FName GroupName, TWeakPtr<FTable> ParentTable, FTableTreeNode& Parent) const
{
	FTableTreeNodePtr NodePtr = MakeShared<FTableTreeNode>(GroupName, ParentTable);
	NodePtr->SetExpansion(false);
	Parent.AddChildAndSetGroupPtr(NodePtr);
	return NodePtr.Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTableTreeNode* FMemAllocGroupingByCallstack::CreateUnsetGroup(TWeakPtr<FTable> ParentTable, FTableTreeNode& Parent) const
{
	static FName NotAvailableName(TEXT("N/A"));
	FTableTreeNodePtr NodePtr = MakeShared<FTableTreeNode>(NotAvailableName, ParentTable);
	NodePtr->SetExpansion(false);
	Parent.AddChildAndSetGroupPtr(NodePtr);
	return NodePtr.Get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
