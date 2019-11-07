// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SteamVREditorCommands.h"
#include "SteamVREditor.h"

#define LOCTEXT_NAMESPACE "FSteamVREditorModule"

void FSteamVREditorCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "SteamVREditor", "Execute SteamVREditor action", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(JsonActionManifest, "Regenerate Action Manifest", "Regenerate Action Manifest", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(JsonControllerBindings, "Regenerate Controller Bindings", "Regenerate Controller Bindings", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(ReloadActionManifest, "Reload Action Manifest", "Reload Action Manifest", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(LaunchBindingsURL, "Launch SteamVR Bindings Dashboard", "Launch SteamVR Bindings Dashboard", EUserInterfaceActionType::Button, FInputGesture());
	UI_COMMAND(AddSampleInputs, "Add Sample Inputs", "Add Sample Inputs", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
