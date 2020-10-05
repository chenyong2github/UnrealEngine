// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/DisplayClusterConfiguratorViewTree.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "Views/TreeViews/DisplayClusterConfiguratorTreeItem.h"
#include "Views/TreeViews/DisplayClusterConfiguratorTreeBuilder.h"
#include "Views/TreeViews/SDisplayClusterConfiguratorViewTree.h"

const FName IDisplayClusterConfiguratorViewTree::Columns::Item("Item");
const FName IDisplayClusterConfiguratorViewTree::Columns::Group("Group");

FDisplayClusterConfiguratorViewTree::FDisplayClusterConfiguratorViewTree(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: ToolkitPtr(InToolkit)
	, bEnabled(false)
{}

TSharedRef<SWidget> FDisplayClusterConfiguratorViewTree::CreateWidget()
{
	ToolkitPtr.Pin()->RegisterOnConfigReloaded(IDisplayClusterConfiguratorToolkit::FOnConfigReloadedDelegate::CreateSP(this, &FDisplayClusterConfiguratorViewTree::OnConfigReloaded));
	ToolkitPtr.Pin()->RegisterOnObjectSelected(IDisplayClusterConfiguratorToolkit::FOnObjectSelectedDelegate::CreateSP(this, &FDisplayClusterConfiguratorViewTree::OnObjectSelected));

	if (!ViewTree.IsValid())
	{
		TreeBuilder->Initialize(SharedThis(this), IDisplayClusterConfiguratorTreeBuilder::FOnFilterConfiguratorTreeItem::CreateSP(this, &FDisplayClusterConfiguratorViewTree::HandleFilterConfiguratorTreeItem));

		SAssignNew(ViewTree, SDisplayClusterConfiguratorViewTree, ToolkitPtr.Pin().ToSharedRef(), TreeBuilder.ToSharedRef(), SharedThis(this));
	}

	return ViewTree.ToSharedRef();
}

void FDisplayClusterConfiguratorViewTree::OnConfigReloaded()
{
	ViewTree->OnConfigReloaded();
}

void FDisplayClusterConfiguratorViewTree::OnObjectSelected()
{
	ViewTree->OnObjectSelected();
}

EDisplayClusterConfiguratorTreeFilterResult FDisplayClusterConfiguratorViewTree::HandleFilterConfiguratorTreeItem(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem)
{
	return ViewTree->HandleFilterConfiguratonTreeItem(InArgs, InItem);
}

void FDisplayClusterConfiguratorViewTree::SetEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
}

UDisplayClusterConfiguratorEditorData* FDisplayClusterConfiguratorViewTree::GetEditorData() const
{
	return ToolkitPtr.Pin()->GetEditorData();
}

void FDisplayClusterConfiguratorViewTree::SetHoveredItem(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem)
{
	HoveredTreeItemPtr = InTreeItem;
	OnHoveredItemSet.Broadcast(InTreeItem);
}

void FDisplayClusterConfiguratorViewTree::ClearHoveredItem()
{
	HoveredTreeItemPtr = nullptr;
	OnHoveredItemCleared.Broadcast();
}

void FDisplayClusterConfiguratorViewTree::RebuildTree()
{
	ViewTree->RebuildTree();
}

void FDisplayClusterConfiguratorViewTree::SetSelectedItem(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem)
{
	SelectedTreeItemPtr = InTreeItem;
	OnSelectedItemSet.Broadcast(InTreeItem);
}

void FDisplayClusterConfiguratorViewTree::ClearSelectedItem()
{
	SelectedTreeItemPtr = nullptr;
	OnSelectedItemCleared.Broadcast();
}

TSharedPtr<IDisplayClusterConfiguratorTreeItem> FDisplayClusterConfiguratorViewTree::GetSelectedItem() const
{
	return SelectedTreeItemPtr.Pin();
}

TSharedPtr<IDisplayClusterConfiguratorTreeItem> FDisplayClusterConfiguratorViewTree::GetHoveredItem() const
{
	return HoveredTreeItemPtr.Pin();
}

FDelegateHandle FDisplayClusterConfiguratorViewTree::RegisterOnHoveredItemSet(const FOnHoveredItemSetDelegate& Delegate)
{
	return OnHoveredItemSet.Add(Delegate);
}

void FDisplayClusterConfiguratorViewTree::UnregisterOnHoveredItemSet(FDelegateHandle DelegateHandle)
{
	OnHoveredItemSet.Remove(DelegateHandle);
}

FDelegateHandle FDisplayClusterConfiguratorViewTree::RegisterOnHoveredItemCleared(const FOnHoveredItemClearedDelegate& Delegate)
{
	return OnHoveredItemCleared.Add(Delegate);
}

void FDisplayClusterConfiguratorViewTree::UnregisterOnHoveredItemCleared(FDelegateHandle DelegateHandle)
{
	OnHoveredItemCleared.Remove(DelegateHandle);
}

FDelegateHandle FDisplayClusterConfiguratorViewTree::RegisterOnSelectedItemSet(const FOnSelectedItemSetDelegate& Delegate)
{
	return OnSelectedItemSet.Add(Delegate);
}

void FDisplayClusterConfiguratorViewTree::UnregisterOnSelectedItemSet(FDelegateHandle DelegateHandle)
{
	OnSelectedItemSet.Remove(DelegateHandle);
}

FDelegateHandle FDisplayClusterConfiguratorViewTree::RegisterOnSelectedItemCleared(const FOnSelectedItemClearedDelegate& Delegate)
{
	return OnSelectedItemCleared.Add(Delegate);
}

void FDisplayClusterConfiguratorViewTree::UnregisterOnSelectedItemCleared(FDelegateHandle DelegateHandle)
{
	OnSelectedItemCleared.Remove(DelegateHandle);
}
