// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commands/RemoteControlCommands.h"

#include "RemoteControlUI/Private/UI/RemoteControlPanelStyle.h"

#define LOCTEXT_NAMESPACE "RemoteControlCommands"

FRemoteControlCommands::FRemoteControlCommands()
	: TCommands<FRemoteControlCommands>
	(
		TEXT("RemoteControl"),
		LOCTEXT("RemoteControl", "Remote Control API"),
		NAME_None,
		FRemoteControlPanelStyle::GetStyleSetName()
	)
{
}

void FRemoteControlCommands::RegisterCommands()
{
	// Toggle Edit Mode
	UI_COMMAND(ToggleEditMode, "Toggle Editing", "Toggles edit mode in remote control panel which enables user to modify the exposed entities.", EUserInterfaceActionType::Check, FInputChord(EModifierKey::Control, EKeys::E));

	// Save Preset
	UI_COMMAND(SavePreset, "Save", "Saves this preset", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::S));

	// Find Preset
	UI_COMMAND(FindPresetInContentBrowser, "Browse to Preset", "Browses to the associated preset and selects it in the most recently used Content Browser (summoning one if necessary)", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::B));

	// Toggle Expose Functions
	UI_COMMAND(ToggleExposeFunctions, "Functions", "Expose a function from available list of options.", EUserInterfaceActionType::Button, FInputChord());

	// Toggle Protocol Mappings
	UI_COMMAND(ToggleProtocolMappings, "Mappings", "View list of protocols mapped to active selection.", EUserInterfaceActionType::Button, FInputChord());

	// Toggle Logic Editor
	UI_COMMAND(ToggleLogicEditor, "Logic", "View the logic applied to active selection.", EUserInterfaceActionType::Button, FInputChord());

	// Delete Entity
	UI_COMMAND(DeleteEntity, "Delete", "Delete the selected  group/exposed entity from the list.", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete));

	// Rename Entity
	UI_COMMAND(RenameEntity, "Rename", "Rename the selected  group/exposed entity.", EUserInterfaceActionType::Button, FInputChord(EKeys::F2));
}

#undef LOCTEXT_NAMESPACE
