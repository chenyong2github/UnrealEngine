// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigBlueprintCommands.h"

#define LOCTEXT_NAMESPACE "ControlRigBlueprintCommands"

void FControlRigBlueprintCommands::RegisterCommands()
{
	UI_COMMAND(DeleteItem, "Delete", "Deletes the selected items and removes their nodes from the graph.", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete));
	UI_COMMAND(ExecuteGraph, "Execute", "Execute the rig graph if On.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(AutoCompileGraph, "Auto Compile", "Auto-compile the rig graph if On.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleEventQueue, "Toggle Event", "Toggle between the current and last running event", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetupEvent, "Setup Event", "Enable the setup mode for the rig", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UpdateEvent, "Forwards Solve", "Run the normal update graph", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(InverseEvent, "Backwards Solve", "Run the inverse graph", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(InverseAndUpdateEvent, "Backwards and Forwards", "Run the inverse graph followed by the update graph", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
