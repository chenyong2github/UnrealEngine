// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/DisplayClusterConfiguratorGraph.h"

#include "DisplayClusterConfiguratorToolkit.h"

void UDisplayClusterConfiguratorGraph::Initialize(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	ToolkitPtr = InToolkit;
}
