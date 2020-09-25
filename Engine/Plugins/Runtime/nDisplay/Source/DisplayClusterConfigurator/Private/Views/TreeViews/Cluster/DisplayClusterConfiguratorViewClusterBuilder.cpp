// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Cluster/DisplayClusterConfiguratorViewClusterBuilder.h"

#include "DisplayClusterConfiguratorEditorData.h"
#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Views/TreeViews/Cluster/TreeItems/DisplayClusterConfiguratorTreeItemCluster.h"



FDisplayClusterConfiguratorViewClusterBuilder::FDisplayClusterConfiguratorViewClusterBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: FDisplayClusterConfiguratorTreeBuilder(InToolkit)
{
}

void FDisplayClusterConfiguratorViewClusterBuilder::Build(FDisplayClusterConfiguratorTreeBuilderOutput& Output)
{
	if (UDisplayClusterConfiguratorEditorData* EditorDataPtr = ConfiguratorTreePtr.Pin()->GetEditorData())
	{
		if (UDisplayClusterConfigurationData* Config = ToolkitPtr.Pin()->GetConfig())
		{
			if (Config->Cluster != nullptr)
			{
				AddCluster(Output, Config, Config->Cluster);
			}
		}
	}
}

void FDisplayClusterConfiguratorViewClusterBuilder::AddCluster(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, UObject* InObjectToEdit)
{
	FName ParentName = NAME_None;
	const FName NodeName = "Cluster";
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemCluster>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.Cluster", true);
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItem::GetTypeId());

	AddClusterNodes(Output, InConfig, InObjectToEdit);
}

void FDisplayClusterConfiguratorViewClusterBuilder::AddClusterNodes(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, UObject* InObjectToEdit)
{
	for (const auto& Node : InConfig->Cluster->Nodes)
	{
		FName ParentName = "Cluster";
		const FName NodeName = *Node.Key;
		TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemCluster>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), Node.Value, "DisplayClusterConfigurator.TreeItems.ClusterNode");
		Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemCluster::GetTypeId());

		for (const auto& Viewport : Node.Value->Viewports)
		{
			AddClusterNodeViewport(Output, InConfig, Viewport.Key, Node.Key, Viewport.Value);
		}
	}
}

void FDisplayClusterConfiguratorViewClusterBuilder::AddClusterNodeViewport(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfig, const FString& NodeId, const FString& ParentNodeId, UObject* InObjectToEdit)
{
	FName ParentName = *ParentNodeId;
	const FName NodeName = *NodeId;
	TSharedRef<IDisplayClusterConfiguratorTreeItem> DisplayNode = MakeShared<FDisplayClusterConfiguratorTreeItemCluster>(NodeName, ConfiguratorTreePtr.Pin().ToSharedRef(), ToolkitPtr.Pin().ToSharedRef(), InObjectToEdit, "DisplayClusterConfigurator.TreeItems.Viewport");
	Output.Add(DisplayNode, ParentName, FDisplayClusterConfiguratorTreeItemCluster::GetTypeId());
}
