// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphCommands.h"

#include "EditorStyleSet.h"


#define LOCTEXT_NAMESPACE "OptimusEditorGraphCommands"


FOptimusEditorGraphCommands::FOptimusEditorGraphCommands() 
	: TCommands<FOptimusEditorGraphCommands>(
		"OptimusEditorGraph", // Context name for fast lookup
		NSLOCTEXT("Contexts", "DeformerGraphEditorGraph", "Deformer Graph Editor Graph"), // Localized context name for displaying
		NAME_None,
		FEditorStyle::GetStyleSetName()
	)
{
}


void FOptimusEditorGraphCommands::RegisterCommands()
{
	UI_COMMAND(PackageNodes, "Collapse to Function", "Convert the selected custom kernel nodes to a shareable function.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UnpackageNodes, "Expand from Function", "Convert the selected kernel function nodes to a custom kernel.", EUserInterfaceActionType::Button, FInputChord());
}


#undef LOCTEXT_NAMESPACE
