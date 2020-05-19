// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScriptableAssetEditorCommands.h"

#define LOCTEXT_NAMESPACE "FScriptableAssetEditorModule"

void FScriptableAssetEditorCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "ScriptableAssetEditor", "Bring up ScriptableAssetEditor window", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
