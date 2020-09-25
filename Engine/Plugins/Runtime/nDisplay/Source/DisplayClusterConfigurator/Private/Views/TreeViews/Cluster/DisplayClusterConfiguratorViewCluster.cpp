// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Cluster/DisplayClusterConfiguratorViewCluster.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "Views/TreeViews/Cluster/DisplayClusterConfiguratorViewClusterBuilder.h"

FDisplayClusterConfiguratorViewCluster::FDisplayClusterConfiguratorViewCluster(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: FDisplayClusterConfiguratorViewTree(InToolkit)
{
	TreeBuilder = MakeShared<FDisplayClusterConfiguratorViewClusterBuilder>(InToolkit);
}
