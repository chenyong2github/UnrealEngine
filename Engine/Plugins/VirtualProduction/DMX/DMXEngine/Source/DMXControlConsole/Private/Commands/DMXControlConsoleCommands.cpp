// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleCommands.h"

#include "Styling/AppStyle.h"
#include "Framework/Commands/Commands.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleCommands"

FDMXControlConsoleCommands::FDMXControlConsoleCommands()
	: TCommands<FDMXControlConsoleCommands>
	(
		TEXT("DMXControlConsole"),
		NSLOCTEXT("Contexts", "DMXControlConsole", "DMX DMX Control Console"),
		NAME_None,
		FAppStyle::GetAppStyleSetName()
	)
{}

void FDMXControlConsoleCommands::RegisterCommands()
{
	UI_COMMAND(OpenControlConsole, "Open Control Console", "Opens the DMX Control Console", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(PlayDMX, "Play DMX", "Play DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(StopDMX, "Stop Playing DMX", "Stop Playing DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ClearAll, "Clear All", "Clear All.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE 
