// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/DisplayClusterConfiguratorTreeViewCommands.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorTreeViewCommands"

void FDisplayClusterConfiguratorTreeViewCommands::RegisterCommands()
{
	UI_COMMAND(ShowAllNodes, "Show All Nodes", "Show every node in the config", EUserInterfaceActionType::RadioButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE
