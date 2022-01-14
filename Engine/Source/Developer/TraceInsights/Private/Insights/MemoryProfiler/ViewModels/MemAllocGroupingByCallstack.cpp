// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocGroupingByCallstack.h"
#include "TraceServices/Model/Callstack.h"
#include "CallstackFormatting.h"

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
		TEXT("Icons.Group.TreeItem"),
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

			FCallstackGroup** FoundGroupPtrPtr = GroupMapByCallstack.Find(Callstack);
			if (FoundGroupPtrPtr)
			{
				GroupPtr = *FoundGroupPtrPtr;
			}
			else if (Callstack)
			{
				const int32 NumFrames = static_cast<int32>(Callstack->Num());
				for (int32 FrameDepth = NumFrames - 1; FrameDepth >= 0; --FrameDepth)
				{
					const TraceServices::FStackFrame* Frame = Callstack->Frame(bIsInverted ? NumFrames - FrameDepth - 1 : FrameDepth);
					check(Frame != nullptr);

					if (bIsGroupingByFunction)
					{
						const FName GroupName = GetGroupName(Frame);

						// Merge with parent group, if it has the same name (i.e. same function).
						if (GroupPtr->Parent != nullptr && GroupPtr->Name == GroupName)
						{
							GroupPtr = GroupPtr->Parent;
						}

						// Merge groups by name.
						FCallstackGroup** GroupPtrPtr = GroupPtr->GroupMapByName.Find(GroupName);
						if (!GroupPtrPtr)
						{
							GroupPtr = CreateGroup(CallstackGroups, GroupPtr, GroupName, InParentTable, Frame);
							check(GroupPtr->Parent != nullptr);
							GroupPtr->Parent->GroupMapByName.Add(GroupName, GroupPtr);
						}
						else
						{
							GroupPtr = *GroupPtrPtr;
						}
					}
					else
					{
						// Merge groups by unique callstack frame.
						FCallstackGroup** GroupPtrPtr = GroupPtr->GroupMap.Find(Frame->Addr);
						if (!GroupPtrPtr)
						{
							const FName GroupName = GetGroupName(Frame);
							GroupPtr = CreateGroup(CallstackGroups, GroupPtr, GroupName, InParentTable, Frame);
							check(GroupPtr->Parent != nullptr);
							GroupPtr->Parent->GroupMap.Add(Frame->Addr, GroupPtr);
						}
						else
						{
							GroupPtr = *GroupPtrPtr;
						}
					}
				}

				GroupMapByCallstack.Add(Callstack, GroupPtr);
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
		FStringView GroupName(Frame->Symbol->Name);
		if (GroupName.Len() >= NAME_SIZE)
		{
			GroupName = FStringView(Frame->Symbol->Name, NAME_SIZE - 1);
		}
		return FName(GroupName, 0);
	}
	else if (Result == TraceServices::ESymbolQueryResult::Pending)
	{
		return FName(FString::Printf(TEXT("0x%X [...]"), Frame->Addr), 0);
	}
	else
	{
		return FName(FString::Printf(TEXT("0x%X"), Frame->Addr), 0);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FMemAllocGroupingByCallstack::GetGroupTooltip(const TraceServices::FStackFrame* Frame) const
{
	TStringBuilder<1024> String;
	FormatStackFrame(*Frame, String, EStackFrameFormatFlags::Module | EStackFrameFormatFlags::FileAndLine);
	return FText::FromString(FString(String));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemAllocGroupingByCallstack::FCallstackGroup* FMemAllocGroupingByCallstack::CreateGroup(
	TArray<FCallstackGroup*>& InOutAllCallstackGroup,
	FCallstackGroup* InParentGroup,
	const FName InGroupName,
	TWeakPtr<FTable> InParentTable,
	const TraceServices::FStackFrame* InFrame) const
{
	FCallstackGroup* NewGroupPtr = new FCallstackGroup();
	NewGroupPtr->Parent = InParentGroup;
	NewGroupPtr->Name = InGroupName;

	InOutAllCallstackGroup.Add(NewGroupPtr);

	FTableTreeNodePtr NodePtr = MakeShared<FTableTreeNode>(InGroupName, InParentTable);
	NodePtr->SetExpansion(false);
	InParentGroup->Node->AddChildAndSetGroupPtr(NodePtr);

	NewGroupPtr->Node = NodePtr.Get();
	NewGroupPtr->Node->SetTooltip(GetGroupTooltip(InFrame));
	NewGroupPtr->Node->SetContext((void*)InFrame);

	return NewGroupPtr;
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
