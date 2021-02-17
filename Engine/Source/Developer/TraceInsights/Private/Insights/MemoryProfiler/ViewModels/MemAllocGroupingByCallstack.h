// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"
#include "Insights/Table/ViewModels/TreeNodeGrouping.h"

namespace TraceServices
{
	struct FStackFrame;
}

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemAllocGroupingByCallstack : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FMemAllocGroupingByCallstack, FTreeNodeGrouping)

public:
	FMemAllocGroupingByCallstack(bool bInIsInverted, bool bInIsGroupingByFunction);
	virtual ~FMemAllocGroupingByCallstack();

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const override;

	bool IsInverted() const { return bIsInverted; }

	bool IsGroupingByFunction() const { return bIsGroupingByFunction; }
	void SetGroupingByFunction(bool bOnOff) { bIsGroupingByFunction = bOnOff; }

private:
	FName GetGroupName(const TraceServices::FStackFrame* Frame) const;
	FText GetGroupTooltip(const TraceServices::FStackFrame* Frame) const;

	FTableTreeNode* CreateGroup(const FName GroupName, TWeakPtr<FTable> ParentTable, FTableTreeNode& Parent) const;
	FTableTreeNode* CreateUnsetGroup(TWeakPtr<FTable> ParentTable, FTableTreeNode& Parent) const;

private:
	bool bIsInverted;
	bool bIsGroupingByFunction;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
