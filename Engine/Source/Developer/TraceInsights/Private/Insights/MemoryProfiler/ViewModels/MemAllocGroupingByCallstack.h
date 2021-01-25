// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"
#include "Insights/Table/ViewModels/TreeNodeGrouping.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemAllocGroupingByCallstack : public FTreeNodeGrouping
{
	INSIGHTS_DECLARE_RTTI(FMemAllocGroupingByCallstack, FTreeNodeGrouping)

public:
	FMemAllocGroupingByCallstack(bool bInIsInverted);
	virtual ~FMemAllocGroupingByCallstack();

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const override;

	bool IsInverted() const { return bIsInverted; }

private:
	bool bIsInverted;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
