// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/DisplayClusterConfiguratorTreeViewCommands.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorTreeViewCommands"

void FDisplayClusterConfiguratorTreeViewCommands::RegisterCommands()
{
	UI_COMMAND(ShowAllNodes, "Show All Nodes", "Show every node in the config", EUserInterfaceActionType::RadioButton, FInputChord());

	UI_COMMAND(SetAsMaster, "Set as Master", "Sets this cluster node as the master node", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
