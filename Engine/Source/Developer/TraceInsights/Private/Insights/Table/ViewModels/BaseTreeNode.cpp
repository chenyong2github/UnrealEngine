// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseTreeNode.h"

// Insights
#include "Insights/Table/ViewModels/TableCellValueSorter.h"

#define LOCTEXT_NAMESPACE "Insights_TreeNode"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FBaseTreeNode::GetDisplayName() const
{
	FText Text = FText::GetEmpty();

	if (IsGroup())
	{
		const int32 NumChildren = Children.Num();
		const int32 NumFilteredChildren = FilteredChildren.Num();

		if (NumFilteredChildren == NumChildren)
		{
			Text = FText::Format(LOCTEXT("TreeNodeGroupTextFmt1", "{0} ({1})"), FText::FromName(GetName()), FText::AsNumber(NumChildren));
		}
		else
		{
			Text = FText::Format(LOCTEXT("TreeNodeGroupTextFmt2", "{0} ({1} / {2})"), FText::FromName(GetName()), FText::AsNumber(NumFilteredChildren), FText::AsNumber(NumChildren));
		}
	}
	else
	{
		Text = FText::FromName(GetName());
	}

	return Text;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FBaseTreeNode::SortChildrenAscending(const ITableCellValueSorter& Sorter)
{
	Sorter.Sort(Children, ESortMode::Ascending);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FBaseTreeNode::SortChildrenDescending(const ITableCellValueSorter& Sorter)
{
	Sorter.Sort(Children, ESortMode::Descending);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
