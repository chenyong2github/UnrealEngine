// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseCharacterFXEditorCommands.h"

class CHAOSCLOTHASSETEDITOR_API FChaosClothAssetEditorCommands : public TBaseCharacterFXEditorCommands<FChaosClothAssetEditorCommands>
{
public:

	FChaosClothAssetEditorCommands();

	// TBaseCharacterFXEditorCommands<> interface
	virtual void RegisterCommands() override;

public:

	TSharedPtr<FUICommandInfo> OpenClothEditor;

	const static FString BeginRemeshToolIdentifier;
	TSharedPtr<FUICommandInfo> BeginRemeshTool;
	const static FString BeginWeightMapPaintToolIdentifier;
	TSharedPtr<FUICommandInfo> BeginWeightMapPaintTool;

	const static FString BeginAttributeEditorToolIdentifier;
	TSharedPtr<FUICommandInfo> BeginAttributeEditorTool;

	// Rest space viewport commands
	const static FString TogglePatternModeIdentifier;
	TSharedPtr<FUICommandInfo> TogglePatternMode;

	// Sim viewport commands
	const static FString ToggleSimMeshWireframeIdentifier;
	TSharedPtr<FUICommandInfo> ToggleSimMeshWireframe;

	const static FString ToggleRenderMeshWireframeIdentifier;
	TSharedPtr<FUICommandInfo> ToggleRenderMeshWireframe;

};
