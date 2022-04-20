// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeOfDayEditorCommands.h"

#define LOCTEXT_NAMESPACE "FTimeOfDayEditorModule"

void FTimeOfDayEditorCommands::RegisterCommands()
{
	UI_COMMAND(OpenTimeOfDayEditor, "TimeOfDay", "Opens the time Time of Day Editor", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
