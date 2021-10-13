// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesEditorCommands.h"

#include "ConsoleVariablesEditorStyle.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

void FConsoleVariablesEditorCommands::RegisterCommands()
{
	FUICommandInfo::MakeCommandInfo(
		this->AsShared(),
		OpenConsoleVariablesEditorMenuItem,
		FName("OpenConsoleVariablesEditorMenuItem"),
		LOCTEXT("OpenConsoleVariablesEditorMenuItem", "Console Variables"),
		LOCTEXT("OpenConsoleVariablesEditorTooltip", "Open Console Variables Editor"),
		FSlateIcon(FConsoleVariablesEditorStyle::Get().GetStyleSetName(), "ConsoleVariables.ToolbarButton", "ConsoleVariables.ToolbarButton.Small"),
		EUserInterfaceActionType::Button,
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE
