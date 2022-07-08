// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorCommands.h"

#include "Framework/Commands/InputChord.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PCGEditorCommands"

FPCGEditorCommands::FPCGEditorCommands()
	: TCommands<FPCGEditorCommands>(
		"PCGEditor",
		NSLOCTEXT("Contexts", "PCGEditor", "PCG Editor"),
		NAME_None,
		FAppStyle::GetAppStyleSetName())
{
}

void FPCGEditorCommands::RegisterCommands()
{
	UI_COMMAND(CollapseNodes, "Collapse into Subgraph", "Collapse selected nodes into a separate PCGGraph asset.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Alt, EKeys::J));
	UI_COMMAND(Find, "Find", "Finds PCG nodes and comments in the current graph", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::F));
	UI_COMMAND(RunDeterminismTest, "Run Determinism Test", "Evaluate the current node for determinism.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Alt, EKeys::D));
	UI_COMMAND(StartInspectNode, "Start Inspecting Node", "Start Inspecting Node", EUserInterfaceActionType::Button, FInputChord(EKeys::I));
	UI_COMMAND(StopInspectNode, "Stop Inspecting Node", "Stop Inspecting Node", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift, EKeys::I));
}

#undef LOCTEXT_NAMESPACE
