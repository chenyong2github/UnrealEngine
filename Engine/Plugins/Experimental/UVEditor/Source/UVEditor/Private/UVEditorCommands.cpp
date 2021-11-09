// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorCommands.h"

#include "EditorStyleSet.h"
#include "Framework/Commands/InputChord.h"
#include "UVEditorStyle.h"

#define LOCTEXT_NAMESPACE "FUVEditorCommands"
	
FUVEditorCommands::FUVEditorCommands()
	: TCommands<FUVEditorCommands>("UVEditor",
		LOCTEXT("ContextDescription", "UV Editor"), 
		NAME_None, // Parent
		FUVEditorStyle::Get().GetStyleSetName()
		)
{
}

void FUVEditorCommands::RegisterCommands()
{
	// These are part of the asset editor UI
	UI_COMMAND(OpenUVEditor, "Open UV Editor", "Open the UV Editor window", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ApplyChanges, "Apply Changes", "Apply changes without yet saving the assets", EUserInterfaceActionType::Button, FInputChord());

	// These get linked to various tool buttons.
	UI_COMMAND(BeginSelectTool, "Edit", "Switch to selection-based edit tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginLayoutTool, "Layout", "Switch to layout tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginParameterizeMeshTool, "Auto UV", "Switch to Auto UV tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginChannelEditTool, "Channel Edit", "Switch to Channel Edit tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginSeamTool, "Seam", "Switch to Seam tool", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(BeginRecomputeUVsTool, "Unwrap", "Switch to Unwrap tool", EUserInterfaceActionType::ToggleButton, FInputChord());

	// These allow us to link up to pressed keys
	UI_COMMAND(AcceptOrCompleteActiveTool, "Accept", "Accept the active tool", EUserInterfaceActionType::Button, FInputChord(EKeys::Enter));
	UI_COMMAND(CancelOrCompleteActiveTool, "Cancel", "Cancel the active tool", EUserInterfaceActionType::Button, FInputChord(EKeys::Escape));

	// These get used in viewport buttons
	UI_COMMAND(VertexSelection, "Vertex Selection", "Select vertex on click", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(EdgeSelection, "Edge Selection", "Select edge on click", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(TriangleSelection, "Triangle Selection", "Select triangle on click", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(IslandSelection, "Island Selection", "Select connected island on click", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(FullMeshSelection, "Full Mesh Selection", "Select whole mesh on click", EUserInterfaceActionType::ToggleButton, FInputChord());

	UI_COMMAND(EnableOrbitCamera, "Orbit", "Enable Orbit Camera", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(EnableFlyCamera, "Fly", "Enable Fly Camera", EUserInterfaceActionType::ToggleButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE
