// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditModeCommands.h"

#define LOCTEXT_NAMESPACE "ControlRigEditModeCommands"

void FControlRigEditModeCommands::RegisterCommands()
{
	UI_COMMAND(ResetTransforms, "Reset Transform", "Reset the Controls Transforms", EUserInterfaceActionType::Button, FInputChord(EKeys::G, EModifierKey::Control));
	UI_COMMAND(ResetAllTransforms, "Reset All Transform", "Reset all of the Controls Transforms", EUserInterfaceActionType::Button, FInputChord(EKeys::G, EModifierKey::Control | EModifierKey::Shift));
	UI_COMMAND(ToggleManipulators, "Toggle Manipulators", "Toggles visibility of manipulators in the viewport", EUserInterfaceActionType::Button, FInputChord(EKeys::T));
	UI_COMMAND(FrameSelection, "Frame Selection", "Focus the viewport on the current selection", EUserInterfaceActionType::Button, FInputChord(EKeys::F));
	UI_COMMAND(ClearSelection, "Clear Selection", "Clear Selection", EUserInterfaceActionType::Button, FInputChord(EKeys::Escape));

	UI_COMMAND(IncreaseGizmoSize, "Increase Gizmo Size", "Increase Size of the Gizmos In The Viewport", EUserInterfaceActionType::Button, FInputChord(EKeys::Equals, EModifierKey::Shift));
	UI_COMMAND(DecreaseGizmoSize, "Decrease Gizmo Size", "Decrease Size of the Gizmos In The Viewport", EUserInterfaceActionType::Button, FInputChord(EKeys::Hyphen, EModifierKey::Shift));
	UI_COMMAND(ResetGizmoSize, "Reset Gizmo Size", "Resize Gizmo Size To Default", EUserInterfaceActionType::Button, FInputChord(EKeys::Equals));


}

#undef LOCTEXT_NAMESPACE
