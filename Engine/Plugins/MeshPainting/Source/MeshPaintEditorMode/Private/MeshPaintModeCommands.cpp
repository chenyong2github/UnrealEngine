// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshPaintModeCommands.h"
#include "Framework/Commands/UIAction.h"
#include "MeshPaintMode.h"
#include "SingleSelectionTool.h"
#include "MeshVertexPaintingTool.h"

#define LOCTEXT_NAMESPACE "MeshPaintEditorModeCommands"



void FMeshPaintingToolActionCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{
	ToolCDOs.Add(GetMutableDefault<USingleSelectionTool>());
	ToolCDOs.Add(GetMutableDefault<UMeshColorPaintingTool>());
}




void FMeshPaintingToolActionCommands::RegisterAllToolActions()
{
	FMeshPaintingToolActionCommands::Register();
}

void FMeshPaintingToolActionCommands::UnregisterAllToolActions()
{
	FMeshPaintingToolActionCommands::Unregister();
}

void FMeshPaintingToolActionCommands::UpdateToolCommandBinding(UInteractiveTool* Tool, TSharedPtr<FUICommandList> UICommandList, bool bUnbind)
{
	if (FMeshPaintingToolActionCommands::IsRegistered())
	{
		!bUnbind ? FMeshPaintingToolActionCommands::Get().BindCommandsForCurrentTool(UICommandList, Tool) : FMeshPaintingToolActionCommands::Get().UnbindActiveCommands(UICommandList);
	}
}




void FMeshPaintEditorModeCommands::RegisterCommands()
{
	TArray<TSharedPtr<FUICommandInfo>> ColorCommands;
	TArray<TSharedPtr<FUICommandInfo>> WeightCommands;
	TArray<TSharedPtr<FUICommandInfo>> VertexCommands;
	 
	UI_COMMAND(Select, "Select", "Select the mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	ColorCommands.Add(Select);
	WeightCommands.Add(Select);
	UI_COMMAND(ColorPaint, "Paint", "Paint the mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	ColorCommands.Add(ColorPaint);
	UI_COMMAND(WeightPaint, "Paint", "Paint the mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	WeightCommands.Add(WeightPaint);
	UI_COMMAND(Fill, "Fill", "Fills the selected Meshes with the Paint Color", EUserInterfaceActionType::Button, FInputChord());
	VertexCommands.Add(Fill);
	UI_COMMAND(Propagate, "Apply", "Propagates Instance Vertex Colors to the Source Meshes", EUserInterfaceActionType::Button, FInputChord());
	VertexCommands.Add(Propagate);
 	UI_COMMAND(Import, "Import", "Imports Vertex Colors from a TGA Texture File to the Selected Meshes", EUserInterfaceActionType::Button, FInputChord());
 	VertexCommands.Add(Import);
 	UI_COMMAND(Save, "Save", "Saves the Source Meshes for the selected Mesh Components", EUserInterfaceActionType::Button, FInputChord());
	VertexCommands.Add(Save);
	UI_COMMAND(Copy, "Copy", "Copies Vertex Colors from the selected Mesh Components", EUserInterfaceActionType::Button, FInputChord());
 	VertexCommands.Add(Copy);
	UI_COMMAND(Paste, "Paste", "Tried to Paste Vertex Colors on the selected Mesh Components", EUserInterfaceActionType::Button, FInputChord());
 	VertexCommands.Add(Paste);
	UI_COMMAND(Remove, "Remove", "Removes Vertex Colors from the selected Mesh Components", EUserInterfaceActionType::Button, FInputChord());
	VertexCommands.Add(Remove);
	UI_COMMAND(Fix, "Fix", "If necessary fixes Vertex Colors applied to the selected Mesh Components", EUserInterfaceActionType::Button, FInputChord());
	VertexCommands.Add(Fix);
	UI_COMMAND(PropagateVertexColorsToLODs, "All LODs", "Applied the Vertex Colors from LOD0 to all LOD levels", EUserInterfaceActionType::Button, FInputChord());
	VertexCommands.Add(PropagateVertexColorsToLODs);
	ColorCommands.Append(VertexCommands);
	WeightCommands.Append(VertexCommands);
	Commands.Add(UMeshPaintMode::MeshPaintMode_Color, ColorCommands);
	Commands.Add(UMeshPaintMode::MeshPaintMode_Weights, WeightCommands);

	TArray<TSharedPtr<FUICommandInfo>> TextureCommands;
	UI_COMMAND(PropagateTexturePaint, "Apply", "Propagates Modifications to the Textures", EUserInterfaceActionType::Button, FInputChord());
	TextureCommands.Add(PropagateTexturePaint);
	UI_COMMAND(SaveTexturePaint, "Save", "Saves the Modified Textures for the selected Mesh Components", EUserInterfaceActionType::Button, FInputChord());
	TextureCommands.Add(SaveTexturePaint);
	Commands.Add(UMeshPaintMode::MeshPaintMode_Texture, TextureCommands);

	// // 	UI_COMMAND(NextTexture, "Next Texture", "Cycle To Next Texture", EUserInterfaceActionType::Button, FInputChord(EKeys::Period));
	// // 	Commands.Add(NextTexture);
	// // 	UI_COMMAND(PreviousTexture, "Previous Texture", "Cycle To Previous Texture", EUserInterfaceActionType::Button, FInputChord(EKeys::Comma));
	// // 	Commands.Add(PreviousTexture);
	// // 
	// // 	UI_COMMAND(CommitTexturePainting, "Commit Texture Painting", "Commits Texture Painting Changes", EUserInterfaceActionType::Button, FInputChord(EKeys::C, EModifierKey::Control | EModifierKey::Shift));
	// // 	Commands.Add(CommitTexturePainting);
	// // 
	// 
	// 
	// 
	// // 
	// // 	UI_COMMAND(SwitchForeAndBackgroundColor, "Switch Fore and Background Color", "Switches the Fore and Background Colors used for Vertex Painting", EUserInterfaceActionType::None, FInputChord(EKeys::X));
	// // 	Commands.Add(SwitchForeAndBackgroundColor);
	// // 
	// 
	// // 
	// // 
	// // 	UI_COMMAND(CycleToNextLOD, "Cycle to next Mesh LOD", "Cycles to the next possible Mesh LOD to Paint on", EUserInterfaceActionType::None, FInputChord(EKeys::N));
	// // 	Commands.Add(CycleToNextLOD);
	// // 
	// // 	UI_COMMAND(CycleToPreviousLOD, "Cycle to previous Mesh LOD", "Cycles to the previous possible Mesh LOD to Paint on", EUserInterfaceActionType::None, FInputChord(EKeys::B));
	// // 	Commands.Add(CycleToPreviousLOD);
}

#undef LOCTEXT_NAMESPACE

