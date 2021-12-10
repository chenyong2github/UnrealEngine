// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commands/RemoteControlCommands.h"

#include "RemoteControlUI/Private/UI/RemoteControlPanelStyle.h"

#define LOCTEXT_NAMESPACE "RemoteControlCommands"

FRemoteControlCommands::FRemoteControlCommands()
	: TCommands<FRemoteControlCommands>
	(
		TEXT("RemoteControl"),
		LOCTEXT("RemoteControl", "Remote Control API"),
		NAME_None,
		FRemoteControlPanelStyle::GetStyleSetName()
	)
{
}

void FRemoteControlCommands::RegisterCommands()
{
	// Toggle Edit Mode

	UI_COMMAND(ToggleEditMode, "Toggle Editing", "Toggles edit mode in remote control panel which enables user to modify the exposed entities.", EUserInterfaceActionType::Check, FInputChord(EModifierKey::Control, EKeys::E));
}

#undef LOCTEXT_NAMESPACE
