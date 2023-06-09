// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorCommands.h"

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"


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

	UI_COMMAND(CreateNewConsole, "New Console", "Creates a new Console", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::N));
	UI_COMMAND(SaveConsole, "Save Console", "Saves the Console", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::S));
	UI_COMMAND(SaveConsoleAs, "Save Console As...", "Saves the Console as a new asset", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control | EModifierKey::Shift, EKeys::S));
	UI_COMMAND(SendDMX, "Send DMX", "Send DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(StopDMX, "Stop Sending DMX", "Stop Sending DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RemoveElements, "Remove Elements", "Remove selected elements from current Control Console", EUserInterfaceActionType::None, FInputChord(EKeys::Delete));
	UI_COMMAND(SelectAll, "Select All", "Select all visible Elements in Control Console", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::A));
	UI_COMMAND(ClearAll, "Clear All", "Clear All.", EUserInterfaceActionType::Button, FInputChord());
	
	UI_COMMAND(Mute, "Mute", "Mute selected Fader Groups.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(MuteAll, "Mute All", "Mute all Fader Groups.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Unmute, "Unmute", "Unmute selected Fader Groups.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UnmuteAll, "Unmute All", "Mute all Fader Groups.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE 
