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

	UI_COMMAND(Save, "Save", "Saves the control console to the currently loaded Preset", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::S));
	UI_COMMAND(SaveAs, "Save As", "Saves the control console as Preset", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::S));
	UI_COMMAND(Load, "Load", "Loads a Control Console Preset", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::O));
	UI_COMMAND(SendDMX, "Send DMX", "Send DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(StopDMX, "Stop Sending DMX", "Stop Sending DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ClearAll, "Clear All", "Clear All.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE 
