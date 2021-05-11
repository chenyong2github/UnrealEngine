// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Cluster/TreeItems/DisplayClusterConfiguratorTreeItemCluster.h"

#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"


FDisplayClusterConfiguratorTreeItemCluster::FDisplayClusterConfiguratorTreeItemCluster(const FName& InName,
	const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
	const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit,
	UObject* InObjectToEdit,
	FString InIconStyle,
	bool InbRoot)
	: FDisplayClusterConfiguratorTreeItem(InViewTree, InToolkit, InObjectToEdit, InbRoot)
	, Name(InName)
	, IconStyle(InIconStyle)
{}

void FDisplayClusterConfiguratorTreeItemCluster::OnItemDoubleClicked()
{
	if (ToolkitPtr.IsValid())
	{
		TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
		Toolkit->GetViewOutputMapping()->JumpToObject(GetObject());
	}
}

void FDisplayClusterConfiguratorTreeItemCluster::OnMouseEnter()
{
	ViewTreePtr.Pin()->SetHoveredItem(SharedThis(this));
}

void FDisplayClusterConfiguratorTreeItemCluster::OnMouseLeave()
{
	ViewTreePtr.Pin()->ClearHoveredItem();
}

bool FDisplayClusterConfiguratorTreeItemCluster::IsHovered() const
{
	if (TSharedPtr<IDisplayClusterConfiguratorTreeItem> HoveredItem = ViewTreePtr.Pin()->GetHoveredItem())
	{
		return HoveredItem.Get() == this;
	}

	return false;
}
