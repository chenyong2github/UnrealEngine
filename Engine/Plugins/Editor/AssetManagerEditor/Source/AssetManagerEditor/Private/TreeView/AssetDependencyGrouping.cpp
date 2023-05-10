// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDependencyGrouping.h"
#include "AssetTreeNode.h"
#include "Insights/Common/AsyncOperationProgress.h"

#define LOCTEXT_NAMESPACE "FAssetDependencyGrouping"

INSIGHTS_IMPLEMENT_RTTI(FAssetDependencyGrouping)

FAssetDependencyGrouping::FAssetDependencyGrouping()
	: FTreeNodeGrouping(
		LOCTEXT("Grouping_ByDependency_ShortName", "Dependency"),
		LOCTEXT("Grouping_ByDependency_TitleName", "By Dependency"),
		LOCTEXT("Grouping_ByDependency_Desc", "Group assets based on their dependency."),
		TEXT("Icons.Group.TreeItem"),
		nullptr)
{
}

FAssetDependencyGrouping::~FAssetDependencyGrouping()
{
}

void FAssetDependencyGrouping::GroupNodes(const TArray<UE::Insights::FTableTreeNodePtr>& Nodes, UE::Insights::FTableTreeNode& ParentGroup, TWeakPtr<UE::Insights::FTable> InParentTable, UE::Insights::IAsyncOperationProgress& InAsyncOperationProgress) const
{
	using namespace UE::Insights;

	ParentGroup.ClearChildren();

	TSharedPtr<FAssetTable> AssetTable = StaticCastSharedPtr<FAssetTable>(InParentTable.Pin());
	check(AssetTable.IsValid());

	const FSlateBrush* IconBrush = FBaseTreeNode::GetDefaultIcon(true);
	FLinearColor AssetGroupNodeColor(1.0f, 1.0f, 1.0f, 1.0f);
	FLinearColor DependenciesGroupNodeColor(0.75f, 0.5f, 1.0f, 1.0f);

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

		// For each (visible) asset, we will create the following hierarcy:
		//
		// |
		// +-- [group:{AssetName}]
		// |   |
		// |   +-- [asset:{AssetName}] // current asset
		// |   |
		// |   +-- [group:Dependencies]
		// |       |
		// |       +-- [asset:{DependentAsset1}] // dependent of current asset
		// |       |
		// |       +-- [asset:{DependentAsset2}] // another dependent of current asset
		// |       |

		FAssetTreeNode& AssetNode = NodePtr->As<FAssetTreeNode>();
		const FAssetTableRow& Asset = AssetNode.GetAssetChecked();

		FTableTreeNodePtr GroupPtr = MakeShared<FCustomTableTreeNode>(FName(Asset.GetName(), 0), InParentTable, IconBrush, AssetGroupNodeColor);
		GroupPtr->SetExpansion(false);
		ParentGroup.AddChildAndSetGroupPtr(GroupPtr);

		GroupPtr->AddChildAndSetGroupPtr(NodePtr);

		FTableTreeNodePtr DepGroupPtr = nullptr;
		const TArray<int32>& Dependencies = Asset.GetDependencies();
		if (Dependencies.Num() > 0)
		{
			DepGroupPtr = MakeShared<FCustomTableTreeNode>(FName(TEXT("Dependencies"), 0), InParentTable, IconBrush, DependenciesGroupNodeColor);
			DepGroupPtr->SetExpansion(false);
			GroupPtr->AddChildAndSetGroupPtr(DepGroupPtr);
		}
		for (int32 DepAssetIndex : Dependencies)
		{
			if (!ensure(AssetTable->IsValidRowIndex(DepAssetIndex)))
			{
				continue;
			}
			const FAssetTableRow& DepAsset = AssetTable->GetAssetChecked(DepAssetIndex);
			FName DepAssetNodeName(DepAsset.GetName());
			FAssetTreeNodePtr DepAssetNodePtr = MakeShared<FAssetTreeNode>(DepAssetNodeName, AssetTable, DepAssetIndex);
			DepGroupPtr->AddChildAndSetGroupPtr(DepAssetNodePtr);
		}
	}
}

#undef LOCTEXT_NAMESPACE
