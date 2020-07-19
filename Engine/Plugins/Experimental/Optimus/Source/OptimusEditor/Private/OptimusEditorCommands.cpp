// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorCommands.h"

#define LOCTEXT_NAMESPACE "OptimusEditorCommands"

FOptimusEditorCommands::FOptimusEditorCommands() 
	: TCommands<FOptimusEditorCommands>(
		"OptimusEditor", // Context name for fast lookup
		NSLOCTEXT("Contexts", "OptimusEditor", "Optimus Editor"), // Localized context name for displaying
		NAME_None,
		FEditorStyle::GetStyleSetName()
	)
{

}


void FOptimusEditorCommands::RegisterCommands()
{
	UI_COMMAND(Apply, "Apply", "Apply changes to original material and its use in the world.", EUserInterfaceActionType::Button, FInputChord());

}

#undef LOCTEXT_NAMESPACE
