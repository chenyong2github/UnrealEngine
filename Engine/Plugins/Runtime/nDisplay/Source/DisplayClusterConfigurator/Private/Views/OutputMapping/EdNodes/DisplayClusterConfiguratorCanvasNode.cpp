// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorCanvasNode.h"

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorCanvasNode.h"

#include "DisplayClusterConfigurationTypes.h"

TSharedPtr<SGraphNode> UDisplayClusterConfiguratorCanvasNode::CreateVisualWidget()
{
	return SNew(SDisplayClusterConfiguratorCanvasNode, this, ToolkitPtr.Pin().ToSharedRef());;
}

void UDisplayClusterConfiguratorCanvasNode::AddWindowNode(UDisplayClusterConfiguratorWindowNode* WindowNode)
{
	WindowNode->SetParentCanvas(this);
	ChildWindows.Add(WindowNode);
}

const TArray<UDisplayClusterConfiguratorWindowNode*>& UDisplayClusterConfiguratorCanvasNode::GetChildWindows() const
{
	return ChildWindows;
}