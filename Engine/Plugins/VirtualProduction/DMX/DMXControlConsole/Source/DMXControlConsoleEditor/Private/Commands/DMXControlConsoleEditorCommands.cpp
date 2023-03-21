// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorCommands.h"

#include "Styling/AppStyle.h"
#include "Framework/Commands/Commands.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorCommands"

FDMXControlConsoleEditorCommands::FDMXControlConsoleEditorCommands()
	: TCommands<FDMXControlConsoleEditorCommands>
	(
		TEXT("DMXControlConsoleEditor"),
		LOCTEXT("DMXControlConsoleEditor", "DMX DMX Control Console"),
		NAME_None,
		FAppStyle::GetAppStyleSetName()
	)
{}

void FDMXControlConsoleEditorCommands::RegisterCommands()
{
	UI_COMMAND(OpenControlConsole, "Open Control Console", "Opens the DMX Control Console", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(CreateNewPreset, "New Preset", "Creates a new Preset", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::N));
	UI_COMMAND(SavePreset, "Save Preset", "Saves the Preset", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::S));
	UI_COMMAND(SavePresetAs, "Save Preset As...", "Saves the Preset as a new asset", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::S));
	UI_COMMAND(SendDMX, "Send DMX", "Send DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(StopDMX, "Stop Sending DMX", "Stop Sending DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ClearAll, "Clear All", "Clear All.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE 
