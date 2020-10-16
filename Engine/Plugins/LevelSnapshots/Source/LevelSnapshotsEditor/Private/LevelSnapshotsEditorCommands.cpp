// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorCommands.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void FLevelSnapshotsEditorCommands::RegisterCommands()
{
	UI_COMMAND(Apply, "Apply", "Apply snapshot to the world", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
