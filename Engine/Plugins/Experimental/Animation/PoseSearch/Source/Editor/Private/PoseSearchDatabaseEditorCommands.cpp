// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseEditorCommands.h"

#define LOCTEXT_NAMESPACE "PoseSearchDatabaseEditorCommands"

void FPoseSearchDatabaseEditorCommands::RegisterCommands()
{
	UI_COMMAND(ResetPreviewScene, "Reset Scene", "Reset Scene", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BuildSearchIndex, "Build Index", "Build Index", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
