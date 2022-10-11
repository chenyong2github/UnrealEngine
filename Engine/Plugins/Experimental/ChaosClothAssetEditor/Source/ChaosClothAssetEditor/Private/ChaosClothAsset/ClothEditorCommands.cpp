// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorStyle.h"
#include "ChaosClothAsset/ClothWeightMapPaintTool.h"

#define LOCTEXT_NAMESPACE "FChaosClothAssetEditorCommands"

const FString FChaosClothAssetEditorCommands::BeginRemeshToolIdentifier = TEXT("BeginRemeshTool");
const FString FChaosClothAssetEditorCommands::BeginAttributeEditorToolIdentifier = TEXT("BeginAttributeEditorTool");
const FString FChaosClothAssetEditorCommands::BeginWeightMapPaintToolIdentifier = TEXT("BeginWeightMapPaintTool");
const FString FChaosClothAssetEditorCommands::ToggleSimMeshWireframeIdentifier = TEXT("ToggleSimMeshWireframe");
const FString FChaosClothAssetEditorCommands::ToggleRenderMeshWireframeIdentifier = TEXT("ToggleRenderMeshWireframe");
const FString FChaosClothAssetEditorCommands::TogglePatternModeIdentifier = TEXT("TogglePatternMode");



FChaosClothAssetEditorCommands::FChaosClothAssetEditorCommands()
	: TBaseCharacterFXEditorCommands<FChaosClothAssetEditorCommands>("ChaosClothAssetEditor",
		LOCTEXT("ContextDescription", "Cloth Editor"), 
		NAME_None, // Parent
		FChaosClothAssetEditorStyle::Get().GetStyleSetName())
{
}

void FChaosClothAssetEditorCommands::RegisterCommands()
{
	TBaseCharacterFXEditorCommands::RegisterCommands();

	UI_COMMAND(OpenClothEditor, "Cloth Editor", "Open the Cloth Editor window", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(BeginRemeshTool, "Remesh", "Remesh the selected mesh", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginAttributeEditorTool, "AttrEd", "Edit/configure mesh attributes", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(BeginWeightMapPaintTool, "MapPnt", "Paint Weight Maps on the mesh", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(TogglePatternMode, "TogglePatternMode", "Toggle pattern mode", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(ToggleSimMeshWireframe, "ToggleSimMeshWireframe", "Toggle simulation mesh wireframe", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleRenderMeshWireframe, "ToggleRenderMeshWireframe", "Toggle render mesh wireframe", EUserInterfaceActionType::Button, FInputChord());
}

void FChaosClothAssetEditorCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{
	ToolCDOs.Add(GetMutableDefault<UClothEditorWeightMapPaintTool>());
}

void FChaosClothAssetEditorCommands::UpdateToolCommandBinding(UInteractiveTool* Tool, TSharedPtr<FUICommandList> UICommandList, bool bUnbind)
{
	if (FChaosClothAssetEditorCommands::IsRegistered())
	{
		if (bUnbind)
		{
			FChaosClothAssetEditorCommands::Get().UnbindActiveCommands(UICommandList);
		}
		else
		{
			FChaosClothAssetEditorCommands::Get().BindCommandsForCurrentTool(UICommandList, Tool);
		}
	}
}


#undef LOCTEXT_NAMESPACE
