// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorCommands.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditorCommands"


void FDisplayClusterLightCardEditorCommands::RegisterCommands()
{
	UI_COMMAND(ResetCamera, "Reset Camera", "Resets the camera to focus on the mesh", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(PerspectiveProjection, "Perspective", "A perspective projection from the stage's view origin", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AzimuthalProjection, "Azimuthal", "An azimuthal hemisphere projection from the stage's center pointed upwards", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
