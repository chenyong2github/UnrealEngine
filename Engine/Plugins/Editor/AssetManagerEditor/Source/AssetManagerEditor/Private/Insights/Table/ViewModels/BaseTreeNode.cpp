// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTreeNode.h"

#include "Styling/SlateBrush.h"
#include "Styling/StyleColors.h"

// Insights
#include "Insights/Common/InsightsStyle.h"
#include "Insights/Table/ViewModels/TableCellValueSorter.h"

#define LOCTEXT_NAMESPACE "UE::Insights::FBaseTreeNode"

namespace UE
{
namespace Insights
{

INSIGHTS_IMPLEMENT_RTTI(FBaseTreeNode)

FBaseTreeNode::FGroupNodeData FBaseTreeNode::DefaultGroupData;

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FBaseTreeNode::GetDisplayName() const
{
	return FText::FromName(GetName());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FBaseTreeNode::GetExtraDisplayName() const
{
	if (IsGroup())
	{
		const int32 NumChildren = GroupData->Children.Num();
		const int32 NumFilteredChildren = GroupData->FilteredChildren.Num();

		if (NumFilteredChildren == NumChildren)
		{
			return FText::Format(LOCTEXT("TreeNodeGroup_ExtraText_Fmt1", "({0})"), FText::AsNumber(NumChildren));
		}
		else
		{
			return FText::Format(LOCTEXT("TreeNodeGroup_ExtraText_Fmt2", "({0} / {1})"), FText::AsNumber(NumFilteredChildren), FText::AsNumber(NumChildren));
		}
	}

	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FBaseTreeNode::HasExtraDisplayName() const
{
	return IsGroup();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FBaseTreeNode::GetDefaultIcon(bool bIsGroupNode)
{
	if (bIsGroupNode)
	{
		return FInsightsStyle::GetBrush("Icons.Group.TreeItem");
	}
	else
	{
		return FInsightsStyle::GetBrush("Icons.Leaf.TreeItem");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FBaseTreeNode::GetDefaultColor(bool bIsGroupNode)
{
	if (bIsGroupNode)
	{
		return FLinearColor(1.0f, 0.9f, 0.6f, 1.0f);
	}
	else
	{
		return FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FBaseTreeNode::SortChildrenAscending(const ITableCellValueSorter& Sorter)
{
	Sorter.Sort(GroupData->Children, ESortMode::Ascending);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FBaseTreeNode::SortChildrenDescending(const ITableCellValueSorter& Sorter)
{
	Sorter.Sort(GroupData->Children, ESortMode::Descending);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
} // namespace UE

#undef LOCTEXT_NAMESPACE
