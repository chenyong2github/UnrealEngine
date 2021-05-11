// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/TreeViews/DisplayClusterConfiguratorTreeItem.h"

class IDisplayClusterConfiguratorViewTree;

class FDisplayClusterConfiguratorTreeItemCluster
	: public FDisplayClusterConfiguratorTreeItem
{
public:
	NDISPLAY_TREE_ITEM_TYPE(FDisplayClusterConfiguratorTreeItemCluster, FDisplayClusterConfiguratorTreeItem)

	FDisplayClusterConfiguratorTreeItemCluster(const FName& InName,
		const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
		const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit,
		UObject* InObjectToEdit,
		FString InIconStyle,
		bool InbRoot = false);

	//~ Begin IDisplayClusterConfiguratorTreeItem Interface
	virtual void OnItemDoubleClicked() override;
	virtual void OnMouseEnter() override;
	virtual void OnMouseLeave() override;
	virtual bool IsHovered() const override;

	virtual FName GetRowItemName() const override { return Name; }
	virtual FString GetIconStyle() const override { return IconStyle; }
	//~ End IDisplayClusterConfiguratorTreeItem Interface

protected:
	FName Name;
	FString IconStyle;
};