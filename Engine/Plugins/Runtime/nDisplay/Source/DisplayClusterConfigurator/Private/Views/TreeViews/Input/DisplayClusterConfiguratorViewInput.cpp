// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Input/DisplayClusterConfiguratorViewInput.h"

#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "Views/TreeViews/Input/DisplayClusterConfiguratorViewInputBuilder.h"

FDisplayClusterConfiguratorViewInput::FDisplayClusterConfiguratorViewInput(const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit)
	: FDisplayClusterConfiguratorViewTree(InToolkit)
{
	TreeBuilder = MakeShared<FDisplayClusterConfiguratorViewInputBuilder>(InToolkit);
}
