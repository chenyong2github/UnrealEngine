// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorCommands.h"

#include "EditorStyleSet.h"
#include "Framework/Commands/InputChord.h"

#define LOCTEXT_NAMESPACE "FUVEditorCommands"
	
FUVEditorCommands::FUVEditorCommands()
	: TCommands<FUVEditorCommands>("UVEditorCommands", 
		NSLOCTEXT("Contexts", "UVEditorCommands", "UV Editor"), 
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // TODO: What should go here?
		)
{
}

void FUVEditorCommands::RegisterCommands()
{
	// These are part of the asset editor UI
	UI_COMMAND(OpenUVEditor, "Open UV Editor", "Open the UV Editor window", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ApplyChanges, "Apply Changes", "Apply changes without yet saving the assets", EUserInterfaceActionType::Button, FInputChord());

	// These get linked to various tool buttons.
	UI_COMMAND(BeginSelectTool, "Select", "Switch to select tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginTransformTool, "Transform", "Switch to transform tool", EUserInterfaceActionType::ToggleButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE
