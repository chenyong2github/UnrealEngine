// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTreeNode.h"

#include "AssetDependencyGrouping.h"
#include "Insights/Common/AsyncOperationProgress.h"
#include "Insights/Common/InsightsStyle.h"
#include "Insights/Table/Widgets/STableTreeView.h"

INSIGHTS_IMPLEMENT_RTTI(FAssetTreeNode)
INSIGHTS_IMPLEMENT_RTTI(FAssetDependenciesGroupTreeNode)

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAssetTreeNode
////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FAssetTreeNode::GetIcon() const
{
#if 0 // debug / mock code
	if (RowId.RowIndex % 7)
	{
		return UE::Insights::FInsightsStyle::GetBrush("Icons.Package.TreeItem");
	}
	if (RowId.RowIndex % 3)
	{
		return UE::Insights::FInsightsStyle::GetBrush("Icons.Asset.TreeItem");
	}
#endif

	//const FAssetTableRow& AssetRpw = GetAssetChecked();
	//TODO: switch (AssetRow->GetType()) --> icon

	return FBaseTreeNode::GetDefaultIcon(false); // default icon for leaf nodes
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FAssetTreeNode::GetColor() const
{
	return GetAssetChecked().GetColor();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAssetDependenciesGroupTreeNode
////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FAssetDependenciesGroupTreeNode::GetColor() const
{
	return FLinearColor(0.75f, 0.5f, 1.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAssetDependenciesGroupTreeNode::OnLazyCreateChildren(TSharedPtr<UE::Insights::STableTreeView> InTableTreeView)
{
	if (bAreChildrenCreated)
	{
		return false;
	}

	TSharedPtr<FAssetTable> AssetTable = StaticCastSharedPtr<FAssetTable>(GetParentTable().Pin());
	const FAssetTableRow& AssetRow = AssetTable->GetAssetChecked(GetRowIndex());
	const TArray<int32>& Dependencies = AssetRow.GetDependencies();
	TArray<UE::Insights::FTableTreeNodePtr> AddedNodes;
	for (int32 DepAssetIndex : Dependencies)
	{
		if (!ensure(AssetTable->IsValidRowIndex(DepAssetIndex)))
		{
			continue;
		}
		const FAssetTableRow& DepAssetRow = AssetTable->GetAssetChecked(DepAssetIndex);
		FName DepAssetNodeName(DepAssetRow.GetName());
		FAssetTreeNodePtr DepAssetNodePtr = MakeShared<FAssetTreeNode>(DepAssetNodeName, AssetTable, DepAssetIndex);
		AddedNodes.Add(DepAssetNodePtr);
	}

	UE::Insights::FAsyncOperationProgress Progress;
	FAssetDependencyGrouping Grouping;
	Grouping.GroupNodes(AddedNodes, *this, GetParentTable(), Progress);

	bAreChildrenCreated = true;
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
