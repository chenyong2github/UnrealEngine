// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/Table/ViewModels/BaseTreeNode.h"
#include "Insights/Table/ViewModels/TreeNodeGrouping.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

class FCallstackGrouping : public FTreeNodeGrouping
{
public:
	FCallstackGrouping(bool bInIsInverted);
	virtual ~FCallstackGrouping();

	virtual void GroupNodes(const TArray<FTableTreeNodePtr>& Nodes, FTableTreeNode& ParentGroup, TWeakPtr<FTable> InParentTable, std::atomic<bool>& bCancelGrouping) const override;

private:
	bool bIsInverted;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
