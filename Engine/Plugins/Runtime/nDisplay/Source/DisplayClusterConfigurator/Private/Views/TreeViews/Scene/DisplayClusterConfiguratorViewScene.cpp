// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Scene/DisplayClusterConfiguratorViewScene.h"

#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "Views/TreeViews/Scene/DisplayClusterConfiguratorViewSceneBuilder.h"

FDisplayClusterConfiguratorViewScene::FDisplayClusterConfiguratorViewScene(const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit)
	: FDisplayClusterConfiguratorViewTree(InToolkit)
{
	TreeBuilder = MakeShared<FDisplayClusterConfiguratorViewSceneBuilder>(InToolkit);
}
