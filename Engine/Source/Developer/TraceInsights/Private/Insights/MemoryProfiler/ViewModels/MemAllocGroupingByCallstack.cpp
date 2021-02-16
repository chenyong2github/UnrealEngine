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

FMemAllocGroupingByCallstack::FMemAllocGroupingByCallstack(bool bInIsInverted)
	: FTreeNodeGrouping(
		bInIsInverted ? LOCTEXT("Grouping_ByCallstack2_ShortName", "Inverted Callstack")
					  : LOCTEXT("Grouping_ByCallstack1_ShortName", "Callstack"),
		bInIsInverted ? LOCTEXT("Grouping_ByCallstack2_TitleName", "By Inverted Callstack")
					  : LOCTEXT("Grouping_ByCallstack1_TitleName", "By Callstack"),
		LOCTEXT("Grouping_Callstack_Desc", "Creates a tree based on callstack of each allocation."),
		TEXT("Profiler.FiltersAndPresets.GroupNameIcon"),
		nullptr)
	, bIsInverted(bInIsInverted)
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
	};
	TArray<FCallstackGroup*> CallstackGroups;

	FCallstackGroup* Root = new FCallstackGroup();
	Root->Node = &ParentGroup;
	CallstackGroups.Add(Root);

	FTableTreeNodePtr UnsetGroupPtr = nullptr;

	static FName NotAvailableName(TEXT("N/A"));

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

					FCallstackGroup** GroupPtrPtr = GroupPtr->GroupMap.Find(Frame->Addr);
					if (!GroupPtrPtr)
					{
						FCallstackGroup* NewGroupPtr = new FCallstackGroup();
						CallstackGroups.Add(NewGroupPtr);
						GroupPtr->GroupMap.Add(Frame->Addr, NewGroupPtr);

						FName GroupName = NotAvailableName;
						const TraceServices::ESymbolQueryResult Result = Frame->Symbol->GetResult();
						switch (Result)
						{
						case TraceServices::ESymbolQueryResult::Pending:
							//GroupName = PendingName;
							GroupName = FName(*FString::Printf(TEXT("0x%X (...)"), Frame->Addr));
							break;
						case TraceServices::ESymbolQueryResult::OK:
							GroupName = FName(Frame->Symbol->Name);
							//GroupName = FName(*FString::Printf(TEXT("0x%X %s"), Frame->Addr, Frame->Symbol->Name));
							break;
						default:
							GroupName = FName(*FString::Printf(TEXT("0x%X"), Frame->Addr));
							break;
						}

						FTableTreeNodePtr NewGroupNodePtr = MakeShared<FTableTreeNode>(GroupName, InParentTable);
						NewGroupPtr->Node = NewGroupNodePtr.Get();
						NewGroupNodePtr->SetExpansion(false);
						GroupPtr->Node->AddChildAndSetGroupPtr(NewGroupNodePtr);

						GroupPtr = NewGroupPtr;
					}
					else
					{
						GroupPtr = *GroupPtrPtr;
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
				UnsetGroupPtr = MakeShared<FTableTreeNode>(NotAvailableName, InParentTable);
				UnsetGroupPtr->SetExpansion(false);
				ParentGroup.AddChildAndSetGroupPtr(UnsetGroupPtr);
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

} // namespace Insights

#undef LOCTEXT_NAMESPACE
