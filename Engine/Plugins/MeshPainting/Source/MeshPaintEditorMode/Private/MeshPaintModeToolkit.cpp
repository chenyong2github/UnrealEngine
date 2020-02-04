// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshPaintModeToolkit.h"
#include "MeshPaintMode.h"

#define LOCTEXT_NAMESPACE "MeshPaintModeToolkit"

void FMeshPaintModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{

	FModeToolkit::Init(InitToolkitHost);
	// ToolsContextClass = UMeshToolsContext::StaticClass();
}

FName FMeshPaintModeToolkit::GetToolkitFName() const
{
	return FName( "MeshPaintMode" );
}


FText FMeshPaintModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT( "ToolkitName", "Mesh Paint Mode" );
}


class UEdMode* FMeshPaintModeToolkit::GetScriptableEditorMode() const
{
	return GLevelEditorModeTools().GetActiveScriptableMode( "MeshPaintMode" );
}

void FMeshPaintModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(UMeshPaintMode::MeshPaintMode_Color);
	PaletteNames.Add(UMeshPaintMode::MeshPaintMode_Weights);
	PaletteNames.Add(UMeshPaintMode::MeshPaintMode_Texture);
}


FText FMeshPaintModeToolkit::GetToolPaletteDisplayName(FName Palette) const
{
	if (Palette == UMeshPaintMode::MeshPaintMode_Color)
	{
		return LOCTEXT("MeshPaintMode_Color", "Colors");
	}
	if (Palette == UMeshPaintMode::MeshPaintMode_Weights)
	{
		return LOCTEXT("MeshPaintMode_Weights", "Weights");
	}
	if (Palette == UMeshPaintMode::MeshPaintMode_Texture)
	{
		return LOCTEXT("MeshPaintMode_Texture", "Textures");
	}
	return FText();
}

FText FMeshPaintModeToolkit::GetActiveToolDisplayName() const 
{ 
	if (UInteractiveTool* ActiveTool = GetScriptableEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left))
	{
		return ActiveTool->GetClass()->GetDisplayNameText();
	}

	return LOCTEXT("MeshPaintNoActiveTool", "Mesh Paint");
}

FText FMeshPaintModeToolkit::GetActiveToolMessage() const 
{ 
	if (UInteractiveTool* ActiveTool = GetScriptableEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left))
	{
		return ActiveToolMessageCache;
	}
	return LOCTEXT("MeshPaintDefaultMessage", "Select a mesh.");
}

void FMeshPaintModeToolkit::SetActiveToolMessage(const FText& Message)
{
	ActiveToolMessageCache = Message;	
}

#undef LOCTEXT_NAMESPACE
