// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditModeCommands.h"

#define LOCTEXT_NAMESPACE "ControlRigEditModeCommands"

void FControlRigEditModeCommands::RegisterCommands()
{
	UI_COMMAND(ResetTransforms, "Reset Transform", "Reset the Controls Transforms", EUserInterfaceActionType::Button, FInputChord(EKeys::G, EModifierKey::Control));
	UI_COMMAND(ResetAllTransforms, "Reset All Transform", "Reset all of the Controls Transforms", EUserInterfaceActionType::Button, FInputChord(EKeys::G, EModifierKey::Control | EModifierKey::Shift));
	UI_COMMAND(ToggleManipulators, "Toggle Manipulators", "Toggles visibility of manipulators in the viewport", EUserInterfaceActionType::Button, FInputChord(EKeys::T));
}

#undef LOCTEXT_NAMESPACE
