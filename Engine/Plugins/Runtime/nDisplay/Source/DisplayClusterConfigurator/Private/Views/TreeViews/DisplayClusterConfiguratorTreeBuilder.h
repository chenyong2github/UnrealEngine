// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeBuilder.h"

class FDisplayClusterConfiguratorToolkit;
class IDisplayClusterConfiguratorTreeItem;
class IDisplayClusterConfiguratorViewTree;

enum class EDisplayClusterConfiguratorTreeFilterResult : uint8;

class FDisplayClusterConfiguratorTreeBuilder
	: public IDisplayClusterConfiguratorTreeBuilder
{
public:
	FDisplayClusterConfiguratorTreeBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin IDisplayClusterConfiguratorTreeBuilder interface
	virtual void Initialize(const TSharedRef<IDisplayClusterConfiguratorViewTree>& InConfiguratorTree, FOnFilterConfiguratorTreeItem InOnFilterTreeItem) override;
	virtual void Filter(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>& InItems, TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>& OutFilteredItems) override;
	virtual EDisplayClusterConfiguratorTreeFilterResult FilterItem(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem) override;
	virtual EDisplayClusterConfiguratorTreeFilterResult FilterRecursive(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem, TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>& OutFilteredItems) override;
	//~ End IDisplayClusterConfiguratorTreeBuilder interface

protected:
	TWeakPtr<IDisplayClusterConfiguratorViewTree> ConfiguratorTreePtr;

	/** Delegate used for filtering */
	FOnFilterConfiguratorTreeItem OnFilterTreeItem;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};
