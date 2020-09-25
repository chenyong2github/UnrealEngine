// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/TreeViews/DisplayClusterConfiguratorTreeItem.h"

class IDisplayClusterConfiguratorViewTree;

class FDisplayClusterConfiguratorTreeItemInput
	: public FDisplayClusterConfiguratorTreeItem
{
public:
	NDISPLAY_TREE_ITEM_TYPE(FDisplayClusterConfiguratorTreeItemInput, FDisplayClusterConfiguratorTreeItem)

	FDisplayClusterConfiguratorTreeItemInput(const FName& InName,
		const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
		UObject* InObjectToEdit,
		FString InIconStyle,
		bool InbRoot = false);

	//~ Begin IDisplayClusterConfiguratorTreeItem Interface
	virtual FName GetRowItemName() const override { return Name; }
	virtual FString GetIconStyle() const override { return IconStyle; }
	//~ End IDisplayClusterConfiguratorTreeItem Interface

private:
	FName Name;
	FString IconStyle;
};