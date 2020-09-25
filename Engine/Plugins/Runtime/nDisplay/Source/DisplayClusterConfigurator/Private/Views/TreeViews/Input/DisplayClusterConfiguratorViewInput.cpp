// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Input/DisplayClusterConfiguratorViewInput.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "Views/TreeViews/Input/DisplayClusterConfiguratorViewInputBuilder.h"

FDisplayClusterConfiguratorViewInput::FDisplayClusterConfiguratorViewInput(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: FDisplayClusterConfiguratorViewTree(InToolkit)
{
	TreeBuilder = MakeShared<FDisplayClusterConfiguratorViewInputBuilder>(InToolkit);
}
