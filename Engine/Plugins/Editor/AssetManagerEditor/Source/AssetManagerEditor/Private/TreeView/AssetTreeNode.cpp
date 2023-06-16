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
INSIGHTS_IMPLEMENT_RTTI(FPluginSimpleGroupNode)
INSIGHTS_IMPLEMENT_RTTI(FPluginAndDependenciesGroupNode)
INSIGHTS_IMPLEMENT_RTTI(FPluginDependenciesGroupNode)

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
// FPluginSimpleGroupNode
////////////////////////////////////////////////////////////////////////////////////////////////////

const FSlateBrush* FPluginSimpleGroupNode::GetIcon() const
{
	return UE::Insights::FBaseTreeNode::GetDefaultIcon(true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FPluginSimpleGroupNode::GetColor() const
{
	return FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FPluginSimpleGroupNode
////////////////////////////////////////////////////////////////////////////////////////////////////

void FPluginSimpleGroupNode::AddAssetChildrenNodes()
{
	// [this]
	// |
	// +-- [asset:{Asset1}]
	// |
	// +-- [asset:{Asset2}]
	// ...

	FAssetTable& AssetTable = GetAssetTableChecked();
	if (AssetTable.IsValidPluginIndex(PluginIndex))
	{
		const FAssetTablePluginInfo& PluginInfo = AssetTable.GetPluginInfoByIndex(PluginIndex);
		AssetTable.EnumerateAssetsForPlugin(PluginInfo, [this, AssetTablePtr = &AssetTable](int32 AssetIndex)
			{
				FName AssetNodeName(AssetTablePtr->GetAssetChecked(AssetIndex).GetName());
				FAssetTreeNodePtr AssetNode = MakeShared<FAssetTreeNode>(AssetNodeName, GetAssetTableWeak(), AssetIndex);
				AddChildAndSetParent(AssetNode);
			});
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FPluginAndDependenciesGroupNode
////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FPluginSimpleGroupNode> FPluginAndDependenciesGroupNode::CreateChildren()
{
	// [this]
	// |
	// +-- [group:Plugin Dependencies] (double click to expand) // FPluginDependenciesGroupNode, lazy
	// |
	// +-- [group:{PluginName}] (self) // FPluginSimpleGroupNode

	FAssetTable& AssetTable = GetAssetTableChecked();
	if (AssetTable.IsValidPluginIndex(PluginIndex))
	{
		// Create the Plugin Dependencies group node.
		// The children nodes (list of dependent plugins) will be lazy created.
		static FName DependenciesGroupName(TEXT("Plugin Dependencies"));
		TSharedPtr<FPluginDependenciesGroupNode> DependenciesGroup = MakeShared<FPluginDependenciesGroupNode>(DependenciesGroupName, GetAssetTableWeak(), PluginIndex);
		AddChildAndSetParent(DependenciesGroup);

		// Create the Plugin Self group node (where asset nodes will be added).
		FName PluginGroupName = AssetTable.GetNameForPlugin(PluginIndex);
		TSharedPtr<FPluginSimpleGroupNode> PluginGroup = MakeShared<FPluginSimpleGroupNode>(PluginGroupName, GetAssetTableWeak(), PluginIndex);
		AddChildAndSetParent(PluginGroup);
		return PluginGroup;
	}
	return SharedThis(this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FPluginDependenciesGroupNode
////////////////////////////////////////////////////////////////////////////////////////////////////

const FText FPluginDependenciesGroupNode::GetExtraDisplayName() const
{
	if (!bAreChildrenCreated)
	{
		return LOCTEXT("DblClickToExpand", "(double click to expand)");
	}
	return FTableTreeNode::GetExtraDisplayName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FPluginDependenciesGroupNode::OnLazyCreateChildren(TSharedPtr<class UE::Insights::STableTreeView> InTableTreeView)
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

	// [this]
	// |
	// +-- [group:{DependentPlugin1}] (self + dependencies) // FPluginAndDependenciesGroupNode
	// |   |
	// |   +-- [group:Plugin Dependencies] (double click to expand) // FPluginDependenciesGroupNode, lazy
	// |   |
	// |   +-- [group:{DependentPlugin1}] (self) // FPluginSimpleGroupNode
	// |       |
	// |       +-- [asset:{Asset1a}]
	// |       |
	// |       +-- [asset:{Asset1b}]
	// |       ...
	// |
	// +-- [group:{DependentPlugin2}] (self, no further dependencies) // FPluginSimpleGroupNode
	// |   |
	// |   +-- [asset:{Asset2a}]
	// |   |
	// |   +-- [asset:{Asset2b}]
	// |   ...
	// |
	// ...

	FAssetTable& AssetTable = GetAssetTableChecked();
	if (AssetTable.IsValidPluginIndex(PluginIndex))
	{
		const FAssetTablePluginInfo& PluginInfo = AssetTable.GetPluginInfoByIndex(PluginIndex);

		// Add dependent plugins.
		for (int32 DependentPluginIndex : PluginInfo.PluginDependencies)
		{
			if (AssetTable.IsValidPluginIndex(DependentPluginIndex))
			{
				FName PluginGroupName = AssetTable.GetNameForPlugin(DependentPluginIndex);
				const FAssetTablePluginInfo& DependentPluginInfo = AssetTable.GetPluginInfoByIndex(DependentPluginIndex);
				if (DependentPluginInfo.PluginDependencies.Num() > 0)
				{
					TSharedPtr<FPluginAndDependenciesGroupNode> PluginGroup = MakeShared<FPluginAndDependenciesGroupNode>(PluginGroupName, GetAssetTableWeak(), DependentPluginIndex);
					PluginGroup->CreateChildren()->AddAssetChildrenNodes();
					AddChildAndSetParent(PluginGroup);
				}
				else
				{
					TSharedPtr<FPluginSimpleGroupNode> PluginGroup = MakeShared<FPluginSimpleGroupNode>(PluginGroupName, GetAssetTableWeak(), DependentPluginIndex);
					PluginGroup->AddAssetChildrenNodes();
					AddChildAndSetParent(PluginGroup);
				}
			}
		}
	}

	bAreChildrenCreated = true;
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
