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
	/** Initialization constructor for the asset node. */
	explicit FAssetTreeNode(const FName InName, TWeakPtr<FAssetTable> InParentTable, int32 InRowIndex)
		: FTableTreeNode(InName, InParentTable, InRowIndex)
		, AssetTablePtr(InParentTable.Pin().Get())
	{
	}

	/** Initialization constructor for the group node. */
	explicit FAssetTreeNode(const FName InGroupName, TWeakPtr<FAssetTable> InParentTable)
		: FTableTreeNode(InGroupName, InParentTable)
		, AssetTablePtr(InParentTable.Pin().Get())
	{
	}

	/** Initialization constructor for the asset and/or group node. */
	explicit FAssetTreeNode(const FName InName, TWeakPtr<FAssetTable> InParentTable, int32 InRowIndex, bool bIsGroup)
		: FTableTreeNode(InName, InParentTable, InRowIndex, bIsGroup)
		, AssetTablePtr(InParentTable.Pin().Get())
	{
	}

	virtual ~FAssetTreeNode() {}

	TWeakPtr<FAssetTable> GetAssetTableWeak() const { return StaticCastWeakPtr<FAssetTable>(GetParentTable()); }

	bool IsValidAsset() const { return AssetTablePtr && AssetTablePtr->IsValidRowIndex(RowId.RowIndex); }
	FAssetTable& GetAssetTableChecked() const { return *AssetTablePtr; }
	const FAssetTableRow& GetAssetChecked() const { return AssetTablePtr->GetAssetChecked(RowId.RowIndex); }

	virtual const FSlateBrush* GetIcon() const override;
	virtual FLinearColor GetColor() const override;

private:
	FAssetTable* AssetTablePtr;
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

class FAssetDependenciesGroupTreeNode : public FAssetTreeNode
{
	INSIGHTS_DECLARE_RTTI(FAssetDependenciesGroupTreeNode, FAssetTreeNode)

public:
	/** Initialization constructor for the group node. */
	explicit FAssetDependenciesGroupTreeNode(const FName InGroupName, TWeakPtr<FAssetTable> InParentTable, int32 InParentRowIndex)
		: FAssetTreeNode(InGroupName, InParentTable, InParentRowIndex, true)
		, bAreChildrenCreated(false)
	{
		// Initially collapsed. Lazy create children when first expanded.
		SetExpansion(false);
	}

	virtual ~FAssetDependenciesGroupTreeNode() {}

	virtual const FText GetExtraDisplayName() const;

	virtual const FSlateBrush* GetIcon() const override;
	virtual FLinearColor GetColor() const override;

	virtual bool OnLazyCreateChildren(TSharedPtr<class UE::Insights::STableTreeView> InTableTreeView) override;

private:
	bool bAreChildrenCreated;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
