// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusToolsCommands.h"

#define LOCTEXT_NAMESPACE "OptimusToolsCommands"


void FOptimusToolsCommands::RegisterCommands()
{
	UI_COMMAND(ToggleModelingToolsMode, "Enable Modeling Tools", "Toggles modeling tools on or off.", EUserInterfaceActionType::ToggleButton, FInputChord());
}

const FOptimusToolsCommands& FOptimusToolsCommands::Get()
{
	return TCommands<FOptimusToolsCommands>::Get();
}

#undef LOCTEXT_NAMESPACE
