// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshPaintModeToolkit.h"
#include "MeshPaintMode.h"

#define LOCTEXT_NAMESPACE "MeshPaintModeToolkit"


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

#undef LOCTEXT_NAMESPACE
