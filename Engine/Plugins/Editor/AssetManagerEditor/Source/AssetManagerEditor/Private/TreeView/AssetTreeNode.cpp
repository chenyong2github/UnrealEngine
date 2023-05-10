// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTreeNode.h"

#include "Insights/Common/InsightsStyle.h"

INSIGHTS_IMPLEMENT_RTTI(FAssetTreeNode)

const FSlateBrush* FAssetTreeNode::GetIcon() const
{
#if 1 // debug / mock code
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

FLinearColor FAssetTreeNode::GetColor() const
{
	return GetAssetChecked().GetColor();
}
