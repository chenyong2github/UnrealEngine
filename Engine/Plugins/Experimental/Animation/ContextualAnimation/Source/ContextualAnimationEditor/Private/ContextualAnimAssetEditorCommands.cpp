// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimAssetEditorCommands.h"

#define LOCTEXT_NAMESPACE "ContextualAnimAssetEditorCommands"

void FContextualAnimAssetEditorCommands::RegisterCommands()
{
	UI_COMMAND(ResetPreviewScene, "Reset Scene", "Reset Scene.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
