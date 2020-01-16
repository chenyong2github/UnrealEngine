// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Toolkits/BaseToolkit.h"
#include "EditorModeManager.h"


/**
 * Mode Toolkit for the Mesh Paint Mode
 */
class FMeshPaintModeToolkit : public FModeToolkit
{
public:

	FMeshPaintModeToolkit(  )
	{}

	// IToolkit overrides
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class UEdMode* GetScriptableEditorMode() const override;
	virtual void GetToolPaletteNames(TArray<FName>& PaletteNames) const override;
	virtual FText GetToolPaletteDisplayName(FName Palette) const override;
};
