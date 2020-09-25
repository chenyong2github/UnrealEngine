// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/IDisplayClusterConfiguratorView.h"

class IDisplayClusterConfiguratorTreeBuilder;
class IDisplayClusterConfiguratorTreeItem;
class UDisplayClusterConfiguratorEditorData;
struct FDisplayClusterConfiguratorTreeFilterArgs;

enum class EDisplayClusterConfiguratorTreeFilterResult : uint8;

enum class EDisplayClusterConfiguratorTreeMode
{
	Editor,
	Picker
};

struct FDisplayClusterConfiguratorTreeArgs
{
	FDisplayClusterConfiguratorTreeArgs()
		: Mode(EDisplayClusterConfiguratorTreeMode::Editor)
		, ContextName(TEXT("ConfiguratorTree"))
		, bShowFilterMenu(true)
	{ }

	/** Optional builder to allow for custom tree construction */
	TSharedPtr<IDisplayClusterConfiguratorTreeBuilder> Builder;

	/** The mode that this items tree is in */
	EDisplayClusterConfiguratorTreeMode Mode;

	/** Context name used to persist settings */
	FName ContextName;

	/** Whether to show the filter menu to allow filtering of active bones, sockets etc. */
	bool bShowFilterMenu;
};

/*
 * Base interface for editor tree view
 */
class IDisplayClusterConfiguratorViewTree
	: public IDisplayClusterConfiguratorView
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHoveredItemSet, const TSharedRef<IDisplayClusterConfiguratorTreeItem>&);
	DECLARE_MULTICAST_DELEGATE(FOnHoveredItemCleared);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelectedItemSet, const TSharedRef<IDisplayClusterConfiguratorTreeItem>&);
	DECLARE_MULTICAST_DELEGATE(FOnSelectedItemCleared);

	using FOnHoveredItemSetDelegate = FOnHoveredItemSet::FDelegate;
	using FOnHoveredItemClearedDelegate = FOnHoveredItemCleared::FDelegate;
	using FOnSelectedItemSetDelegate = FOnSelectedItemSet::FDelegate;
	using FOnSelectedItemClearedDelegate = FOnSelectedItemCleared::FDelegate;

	/**
	 * Type of the tree item
	 */
	struct Columns
	{
		static const FName Item;
		static const FName Group;
	};

	/**
	 * @return The Editor editing UObject
	 */
	virtual UDisplayClusterConfiguratorEditorData* GetEditorData() const = 0;

	/**
	 * Sets currently hovered tree item
	 *
	 * @param InTreeItem	 hovered tree item
	 */
	virtual void SetHoveredItem(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) = 0;

	/**
	 * Remove hovered Item from this tree
	 */
	virtual void ClearHoveredItem() = 0;

	/**
	 * Sets currently selected tree item
	 *
	 * @param InTreeItem	 hovered tree item
	 */
	virtual void SetSelectedItem(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) = 0;

	/**
	 * Remove selected item from this tree
	 */
	virtual void ClearSelectedItem() = 0;

	/**
	 * @return The hovered tree item
	 */
	virtual TSharedPtr<IDisplayClusterConfiguratorTreeItem> GetSelectedItem() const = 0;

	/**
	 * Rebuild the tree from the config 
	 */
	virtual void RebuildTree() = 0;

	/**
	 * @return The hovered tree item
	 */
	virtual TSharedPtr<IDisplayClusterConfiguratorTreeItem> GetHoveredItem() const = 0;

	/**
	 * Register the hovered tree item delegate
	 */
	virtual FDelegateHandle RegisterOnHoveredItemSet(const FOnHoveredItemSetDelegate& Delegate) = 0;

	/**
	 * Remove the hovered tree item delegate
	 */
	virtual void UnregisterOnHoveredItemSet(FDelegateHandle DelegateHandle) = 0;

	/**
	 * Register clear hovered tree item delegate
	 */
	virtual FDelegateHandle RegisterOnHoveredItemCleared(const FOnHoveredItemClearedDelegate& Delegate) = 0;

	/**
	 * Unregister clear hovered tree item delegate
	 */
	virtual void UnregisterOnHoveredItemCleared(FDelegateHandle DelegateHandle) = 0;

	/**
	 * Register the Selected tree item delegate
	 */
	virtual FDelegateHandle RegisterOnSelectedItemSet(const FOnSelectedItemSetDelegate& Delegate) = 0;

	/**
	 * Remove the Selected tree item delegate
	 */
	virtual void UnregisterOnSelectedItemSet(FDelegateHandle DelegateHandle) = 0;

	/**
	 * Register clear Selected tree item delegate
	 */
	virtual FDelegateHandle RegisterOnSelectedItemCleared(const FOnSelectedItemClearedDelegate& Delegate) = 0;

	/**
	 * Unregister clear Selected tree item delegate
	 */
	virtual void UnregisterOnSelectedItemCleared(FDelegateHandle DelegateHandle) = 0;

	/**
	 * Handle filtering the tree 
	 */
	virtual EDisplayClusterConfiguratorTreeFilterResult HandleFilterConfiguratorTreeItem(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem) = 0;
};
