// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseTreeNode.h"

// Insights
#include "Insights/Table/ViewModels/TreeNodeSorting.h"

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

void FBaseTreeNode::SortChildrenAscending(const ITreeNodeSorting* Sorter)
{
	Sorter->SortAscending(Children);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FBaseTreeNode::SortChildrenDescending(const ITreeNodeSorting* Sorter)
{
	Sorter->SortDescending(Children);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
