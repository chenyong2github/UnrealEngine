// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorCommands.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditorCommands"


void FDisplayClusterLightCardEditorCommands::RegisterCommands()
{
	UI_COMMAND(ResetCamera, "Reset Camera", "Resets the camera to focus on the mesh", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
