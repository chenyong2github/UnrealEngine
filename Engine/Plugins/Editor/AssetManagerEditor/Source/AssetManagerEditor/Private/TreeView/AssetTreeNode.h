// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "AssetTable.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

class FAssetTreeNode : public UE::Insights::FTableTreeNode
{
	INSIGHTS_DECLARE_RTTI(FAssetTreeNode, UE::Insights::FTableTreeNode)

public:
	/** Hidden constructor. */
	//explicit FAssetTreeNode(const FName InName, TWeakPtr<UE::Insights::FTable> InParentTable, int32 InRowIndex) = delete;

	/** Hidden constructor. */
	//explicit FAssetTreeNode(const FName InGroupName, TWeakPtr<UE::Insights::FTable> InParentTable) = delete;

	/** Initialization constructor for the asset node. */
	explicit FAssetTreeNode(const FName InName, TWeakPtr<FAssetTable> InParentTable, int32 InRowIndex)
		: FTableTreeNode(InName, InParentTable, InRowIndex)
		, AssetTable(InParentTable.Pin().Get())
	{
	}

	/** Initialization constructor for the group node. */
	explicit FAssetTreeNode(const FName InGroupName, TWeakPtr<FAssetTable> InParentTable)
		: FTableTreeNode(InGroupName, InParentTable)
		, AssetTable(InParentTable.Pin().Get())
	{
	}

	virtual ~FAssetTreeNode() {}

	FAssetTable& GetAssetTableChecked() const { return *AssetTable; }
	const FAssetTableRow& GetAssetChecked() const { return AssetTable->GetAssetChecked(RowId.RowIndex); }

	virtual const FSlateBrush* GetIcon() const override;
	virtual FLinearColor GetColor() const override;

private:
	FAssetTable* AssetTable;
};

/** Type definition for shared pointers to instances of FAssetTreeNode. */
typedef TSharedPtr<class FAssetTreeNode> FAssetTreeNodePtr;

/** Type definition for shared references to instances of FAssetTreeNode. */
typedef TSharedRef<class FAssetTreeNode> FAssetTreeNodeRef;

/** Type definition for shared references to const instances of FAssetTreeNode. */
typedef TSharedRef<const class FAssetTreeNode> FAssetTreeNodeRefConst;

/** Type definition for weak references to instances of FAssetTreeNode. */
typedef TWeakPtr<class FAssetTreeNode> FAssetTreeNodeWeak;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FAssetDependenciesGroupTreeNode : public UE::Insights::FTableTreeNode
{
	INSIGHTS_DECLARE_RTTI(FAssetDependenciesGroupTreeNode, UE::Insights::FTableTreeNode)

public:
	/** Hidden constructor. */
	//explicit FAssetDependenciesGroupTreeNode(const FName InName, TWeakPtr<UE::Insights::FTable> InParentTable, int32 InRowIndex) = delete;

	/** Hidden constructor. */
	//explicit FAssetDependenciesGroupTreeNode(const FName InGroupName, TWeakPtr<UE::Insights::FTable> InParentTable) = delete;

	/** Initialization constructor for the group node. */
	explicit FAssetDependenciesGroupTreeNode(const FName InGroupName, TWeakPtr<FAssetTable> InParentTable, int32 InParentRowIndex)
		: FTableTreeNode(InGroupName, InParentTable, InParentRowIndex)
		, bAreChildrenCreated(false)
	{
		// This is actually a group node.
		InitGroupData();

		// Initially collapsed. Lazy create children when first expanded.
		SetExpansion(false);
	}

	virtual ~FAssetDependenciesGroupTreeNode() {}

	virtual const FText GetExtraDisplayName() const;

	virtual FLinearColor GetColor() const override;

	virtual bool OnLazyCreateChildren(TSharedPtr<class UE::Insights::STableTreeView> InTableTreeView) override;

private:
	bool bAreChildrenCreated;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
