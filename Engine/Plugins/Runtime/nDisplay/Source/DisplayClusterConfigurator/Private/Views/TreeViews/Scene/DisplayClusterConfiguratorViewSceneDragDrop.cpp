// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Scene/DisplayClusterConfiguratorViewSceneDragDrop.h"

#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Views/TreeViews/Scene/TreeItems/DisplayClusterConfiguratorTreeItemScene.h"

TSharedRef<FDisplayClusterConfiguratorViewSceneDragDrop> FDisplayClusterConfiguratorViewSceneDragDrop::New(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree, const TSharedRef<FDisplayClusterConfiguratorTreeItemScene>& InTreeItemScene)
{
	TSharedRef<FDisplayClusterConfiguratorViewSceneDragDrop> Operation = MakeShared<FDisplayClusterConfiguratorViewSceneDragDrop>();

	Operation->ToolkitPtr = InToolkit;
	Operation->ViewTreePtr = InViewTree;
	Operation->TreeItemScenePtr = InTreeItemScene;
	Operation->bCanDrop = false;
	Operation->Construct();
	return Operation;
}

void FDisplayClusterConfiguratorViewSceneDragDrop::SetCanDrop(bool InCanDrop)
{
	bCanDrop = InCanDrop;
}
