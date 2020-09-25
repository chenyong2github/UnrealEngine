// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Scene/DisplayClusterConfiguratorViewScene.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "Views/TreeViews/Scene/DisplayClusterConfiguratorViewSceneBuilder.h"

FDisplayClusterConfiguratorViewScene::FDisplayClusterConfiguratorViewScene(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: FDisplayClusterConfiguratorViewTree(InToolkit)
{
	TreeBuilder = MakeShared<FDisplayClusterConfiguratorViewSceneBuilder>(InToolkit);
}
