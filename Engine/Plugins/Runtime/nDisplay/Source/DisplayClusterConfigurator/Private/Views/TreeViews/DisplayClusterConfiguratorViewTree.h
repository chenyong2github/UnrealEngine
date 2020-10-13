// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"

class IDisplayClusterConfiguratorTreeItem;
class FDisplayClusterConfiguratorTreeBuilder;
class FDisplayClusterConfiguratorToolkit;
class SDisplayClusterConfiguratorViewTree;

struct FDisplayClusterConfiguratorTreeFilterArgs;
enum class EDisplayClusterConfiguratorTreeFilterResult : uint8;

class FDisplayClusterConfiguratorViewTree
	: public IDisplayClusterConfiguratorViewTree
{
public:
	FDisplayClusterConfiguratorViewTree(const TSharedRef<FDisplayClusterConfiguratorToolkit>& Toolkit);

public:
	//~ Begin IDisplayClusterConfiguratorView Interface
	virtual TSharedRef<SWidget> CreateWidget() override;
	virtual EDisplayClusterConfiguratorTreeFilterResult HandleFilterConfiguratorTreeItem(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem) override;
	virtual void SetEnabled(bool bInEnabled) override;

	virtual bool GetIsEnabled() const override { return bEnabled; }
	//~ End IDisplayClusterConfiguratorView Interface

	//~ IDisplayClusterConfiguratorViewTree
	virtual UDisplayClusterConfiguratorEditorData* GetEditorData() const override;
	virtual void SetHoveredItem(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) override;
	virtual void ClearHoveredItem() override;
	virtual void RebuildTree() override;
	virtual void SetSelectedItem(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) override;
	virtual void ClearSelectedItem() override;
	virtual TSharedPtr<IDisplayClusterConfiguratorTreeItem> GetSelectedItem() const override;
	virtual TSharedPtr<IDisplayClusterConfiguratorTreeItem> GetHoveredItem() const override;
	virtual FDelegateHandle RegisterOnHoveredItemSet(const FOnHoveredItemSetDelegate& Delegate) override;
	virtual void UnregisterOnHoveredItemSet(FDelegateHandle DelegateHandle) override;
	virtual FDelegateHandle RegisterOnHoveredItemCleared(const FOnHoveredItemClearedDelegate& Delegate) override;
	virtual void UnregisterOnHoveredItemCleared(FDelegateHandle DelegateHandle) override;
	virtual FDelegateHandle RegisterOnSelectedItemSet(const FOnSelectedItemSetDelegate& Delegate) override;
	virtual void UnregisterOnSelectedItemSet(FDelegateHandle DelegateHandle) override;
	virtual FDelegateHandle RegisterOnSelectedItemCleared(const FOnSelectedItemClearedDelegate& Delegate) override;
	virtual void UnregisterOnSelectedItemCleared(FDelegateHandle DelegateHandle) override;
	//~ IDisplayClusterConfiguratorViewTree

protected:
	virtual void OnConfigReloaded();

	virtual void OnObjectSelected();

protected:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	/** The builder we use to construct the tree */
	TSharedPtr<FDisplayClusterConfiguratorTreeBuilder> TreeBuilder;

	TWeakPtr<IDisplayClusterConfiguratorTreeItem> HoveredTreeItemPtr;

	TWeakPtr<IDisplayClusterConfiguratorTreeItem> SelectedTreeItemPtr;

	FOnHoveredItemSet OnHoveredItemSet;

	FOnHoveredItemCleared OnHoveredItemCleared;

	FOnSelectedItemSet OnSelectedItemSet;

	FOnSelectedItemCleared OnSelectedItemCleared;

	TSharedPtr<SDisplayClusterConfiguratorViewTree> ViewTree;

	bool bEnabled;
};
