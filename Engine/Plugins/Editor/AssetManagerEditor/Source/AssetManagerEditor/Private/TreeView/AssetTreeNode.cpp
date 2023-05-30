// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTreeNode.h"

#include "AssetDependencyGrouping.h"
#include "Insights/Common/AsyncOperationProgress.h"
#include "Insights/Common/InsightsStyle.h"
#include "Insights/Table/Widgets/STableTreeView.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "FAssetTreeNode"

INSIGHTS_IMPLEMENT_RTTI(FAssetTreeNode)
INSIGHTS_IMPLEMENT_RTTI(FAssetDependenciesGroupTreeNode)

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAssetTreeNode
////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FAssetTreeNode::GetIcon() const
{
	if (IsValidAsset())
	{
		return FBaseTreeNode::GetDefaultIcon(false); // default icon for leaf nodes
	}
	return FBaseTreeNode::GetDefaultIcon(IsGroup()); // default icon
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FAssetTreeNode::GetColor() const
{
	return GetAssetChecked().GetColor();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAssetDependenciesGroupTreeNode
////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FAssetDependenciesGroupTreeNode::GetIcon() const
{
	return FBaseTreeNode::GetDefaultIcon(true); // default icon for group nodes
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FAssetDependenciesGroupTreeNode::GetColor() const
{
	return FLinearColor(0.75f, 0.5f, 1.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FAssetDependenciesGroupTreeNode::GetExtraDisplayName() const
{
	if (!bAreChildrenCreated)
	{
		return LOCTEXT("DblClickToExpand", "(double click to expand)");
	}
	return FTableTreeNode::GetExtraDisplayName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAssetDependenciesGroupTreeNode::OnLazyCreateChildren(TSharedPtr<UE::Insights::STableTreeView> InTableTreeView)
{
	if (bAreChildrenCreated)
	{
		return false;
	}

	TSharedPtr<STreeView<UE::Insights::FTableTreeNodePtr>> TreeView = InTableTreeView->GetInnerTreeView();
	if (!TreeView || !TreeView->IsItemExpanded(SharedThis(this)))
	{
		return false;
	}

	FAssetTable& AssetTable = GetAssetTableChecked();
	const FAssetTableRow& AssetRow = AssetTable.GetAssetChecked(GetRowIndex());
	const TArray<int32>& Dependencies = AssetRow.GetDependencies();
	TArray<UE::Insights::FTableTreeNodePtr> AddedNodes;
	for (int32 DepAssetIndex : Dependencies)
	{
		if (!ensure(AssetTable.IsValidRowIndex(DepAssetIndex)))
		{
			continue;
		}
		const FAssetTableRow& DepAssetRow = AssetTable.GetAssetChecked(DepAssetIndex);
		FName DepAssetNodeName(DepAssetRow.GetName());
		FAssetTreeNodePtr DepAssetNodePtr = MakeShared<FAssetTreeNode>(DepAssetNodeName, GetAssetTableWeak(), DepAssetIndex);
		AddedNodes.Add(DepAssetNodePtr);
	}

	UE::Insights::FAsyncOperationProgress Progress;
	FAssetDependencyGrouping Grouping;
	Grouping.GroupNodes(AddedNodes, *this, GetParentTable(), Progress);

	bAreChildrenCreated = true;
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
