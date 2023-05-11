// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDependencyGrouping.h"
#include "AssetTreeNode.h"
#include "Insights/Common/AsyncOperationProgress.h"

#define LOCTEXT_NAMESPACE "FAssetDependencyGrouping"

INSIGHTS_IMPLEMENT_RTTI(FAssetDependencyGrouping)

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAssetDependencyGrouping
////////////////////////////////////////////////////////////////////////////////////////////////////

FAssetDependencyGrouping::FAssetDependencyGrouping()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByDependency_ShortName", "Dependency"),
		LOCTEXT("Grouping_ByDependency_TitleName", "By Dependency"),
		LOCTEXT("Grouping_ByDependency_Desc", "Group assets based on their dependency."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAssetDependencyGrouping::~FAssetDependencyGrouping()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAssetDependencyGrouping::GroupNodes(const TArray<UE::Insights::FTableTreeNodePtr>& Nodes, UE::Insights::FTableTreeNode& ParentGroup, TWeakPtr<UE::Insights::FTable> InParentTable, UE::Insights::IAsyncOperationProgress& InAsyncOperationProgress) const
{
	using namespace UE::Insights;

	ParentGroup.ClearChildren();

	TSharedPtr<FAssetTable> AssetTable = StaticCastSharedPtr<FAssetTable>(InParentTable.Pin());
	check(AssetTable.IsValid());

	const FSlateBrush* AssetGroupNodeIconBrush = FBaseTreeNode::GetDefaultIcon(true);

	for (FTableTreeNodePtr NodePtr : Nodes)
	{
		if (InAsyncOperationProgress.ShouldCancelAsyncOp())
		{
			return;
		}

		if (NodePtr->IsGroup() || !NodePtr->Is<FAssetTreeNode>())
		{
			ParentGroup.AddChildAndSetGroupPtr(NodePtr);
			continue;
		}

		// For each (visible) asset, we will create the following hierarchy:
		//
		// By default, the dependencies node is collapsed.
		// |
		// +-- [group:{AssetName}(Self+Deps)]               *[ASSET0]
		// |   |
		// |   +-- [group:_Self]                            *[ASSET0]
		// |   |   |
		// |   |   +-- [asset:{AssetName}]                  [ASSET0]
		// |   |
		// |   +-- [group:Dependencies]                     *[0]

		// When dependencies node is expanded, it will be populated with actual dependencies.
		// |
		// +-- [group:{AssetName}(Self+Deps)]               *[ASSET0+DEPS0']
		// |   |
		// |   +-- [group:_Self]                            *[ASSET0]
		// |   |   |
		// |   |   +-- [asset:{AssetName}]                  [ASSET0]
		// |   |
		// |   +-- [group:Dependencies]                     *[DEPS0']
		// |       |
		// |       +-- [asset:{DependentAsset1}]            [ASSET1]
		// |       |
		// |       +-- [asset:{DependentAsset2}]            [ASSET2]
		// |       |
		// |       +-- [group:{DependentAsset3}(Self+Deps)] *[ASSET3+DEPS3']
		// |       |
		// |       ...

		FAssetTreeNode& AssetNode = NodePtr->As<FAssetTreeNode>();
		const FAssetTableRow& Asset = AssetNode.GetAssetChecked();

		if (Asset.GetNumDependencies() > 0)
		{
			// Create a group for the asset node (self) + dependencies.
			FTableTreeNodePtr AssetGroupPtr = MakeShared<FCustomTableTreeNode>(Asset.GetNodeName(), InParentTable, AssetGroupNodeIconBrush, Asset.GetColor());
			AssetGroupPtr->SetExpansion(false);
			ParentGroup.AddChildAndSetGroupPtr(AssetGroupPtr);

			// Add the asset node (self) under a "_self" group node.
			static FName SelfGroupName(TEXT("_Self_")); // used _ prefix to sort before "Dependencies"
			FTableTreeNodePtr SelfGroupPtr = MakeShared<FCustomTableTreeNode>(SelfGroupName, InParentTable, AssetGroupNodeIconBrush, Asset.GetColor());
			SelfGroupPtr->SetExpansion(false);
			SelfGroupPtr->AddChildAndSetGroupPtr(NodePtr);
			AssetGroupPtr->AddChildAndSetGroupPtr(SelfGroupPtr);

			// Create a group node for all dependent assets of the current asset.
			// The actual nodes for the dependent assets are lazy created by this group node.
			static FName DependenciesGroupName(TEXT("Dependencies"));
			TSharedPtr<FAssetDependenciesGroupTreeNode> DependenciesGroupPtr = MakeShared<FAssetDependenciesGroupTreeNode>(DependenciesGroupName, AssetTable, AssetNode.GetRowIndex());
			DependenciesGroupPtr->SetExpansion(false);
			AssetGroupPtr->AddChildAndSetGroupPtr(DependenciesGroupPtr);
		}
		else
		{
			// No dependencies. Just add the asset node (self).
			ParentGroup.AddChildAndSetGroupPtr(NodePtr);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
