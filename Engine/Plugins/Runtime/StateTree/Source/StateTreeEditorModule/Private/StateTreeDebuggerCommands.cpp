// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_STATETREE_DEBUGGER

#include "StateTreeDebuggerCommands.h"

#include "StateTreeEditorStyle.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "StateTreeDebugger"

FStateTreeDebuggerCommands::FStateTreeDebuggerCommands()
	: TCommands
	(
		"StateTreeEditor.Debugger",								// Context name for fast lookup
		LOCTEXT("StateTreeDebugger", "StateTree Debugger"),		// Localized context name for displaying
		NAME_None,												// Parent context name
		FStateTreeEditorStyle::Get().GetStyleSetName()
	)
{
}

void FStateTreeDebuggerCommands::RegisterCommands()
{
	UI_COMMAND(ToggleBreakpoint, "Toggle Breakpoint", "Adds or removes a breakpoint on each selected node", EUserInterfaceActionType::Button, FInputChord(EKeys::F9))
	UI_COMMAND(Back, "Back", "Show previous recorded state", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Forward, "Forward", "Show next recorded state", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_STATETREE_DEBUGGER