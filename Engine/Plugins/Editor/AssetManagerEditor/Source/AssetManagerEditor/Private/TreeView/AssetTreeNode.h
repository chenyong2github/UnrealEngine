// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "AssetTable.h"
#include "Insights/Table/ViewModels/TableTreeNode.h"

class FAssetTreeNode : public UE::Insights::FTableTreeNode
{
	INSIGHTS_DECLARE_RTTI(FAssetTreeNode, UE::Insights::FTableTreeNode)

public:
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

	FAssetTable& GetAssetTableChecked() const { return *AssetTable; }
	const FAssetTableRow& GetAssetChecked() const { return AssetTable->GetAssetChecked(RowId.RowIndex); }

	virtual FLinearColor GetColor() const override
	{
		return GetAssetChecked().GetColor();
	}

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
