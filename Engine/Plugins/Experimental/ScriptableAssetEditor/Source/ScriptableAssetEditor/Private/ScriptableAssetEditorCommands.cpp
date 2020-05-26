// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScriptableAssetEditorCommands.h"

#include "Framework/Commands/InputChord.h"

#define LOCTEXT_NAMESPACE "FScriptableAssetEditorModule"

void FScriptableAssetEditorCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "ScriptableAssetEditor", "Bring up ScriptableAssetEditor window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
