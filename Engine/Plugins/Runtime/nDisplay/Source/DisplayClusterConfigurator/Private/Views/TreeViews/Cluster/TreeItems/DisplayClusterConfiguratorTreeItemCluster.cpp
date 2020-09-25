// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Cluster/TreeItems/DisplayClusterConfiguratorTreeItemCluster.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"


FDisplayClusterConfiguratorTreeItemCluster::FDisplayClusterConfiguratorTreeItemCluster(const FName& InName,
	const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
	UObject* InObjectToEdit,
	FString InIconStyle,
	bool InbRoot)
	: FDisplayClusterConfiguratorTreeItem(InViewTree, InToolkit, InObjectToEdit, InbRoot)
	, Name(InName)
	, IconStyle(InIconStyle)
{}


