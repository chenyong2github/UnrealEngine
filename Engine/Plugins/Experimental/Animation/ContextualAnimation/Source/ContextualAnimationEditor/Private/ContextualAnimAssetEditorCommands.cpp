// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimAssetEditorCommands.h"

#define LOCTEXT_NAMESPACE "ContextualAnimAssetEditorCommands"

void FContextualAnimAssetEditorCommands::RegisterCommands()
{
	UI_COMMAND(ResetPreviewScene, "Reset Scene", "Reset Scene.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(NewAnimSet, "New AnimSet", "New AnimSet.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(ShowIKTargetsDrawSelected, "Selected Actor Only", "Show IK Targets for the selected actor", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(ShowIKTargetsDrawAll, "All Actors", "Show IK Targets for all the actors", EUserInterfaceActionType::RadioButton, FInputChord());
	UI_COMMAND(ShowIKTargetsDrawNone, "None", "Hide IK Targets", EUserInterfaceActionType::RadioButton, FInputChord());

	UI_COMMAND(Simulate, "Simulate", "Simulate Mode", EUserInterfaceActionType::RadioButton, FInputChord());

}

#undef LOCTEXT_NAMESPACE
