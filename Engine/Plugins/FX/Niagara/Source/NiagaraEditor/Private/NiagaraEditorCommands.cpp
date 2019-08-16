// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorCommands.h"

#include "Framework/Commands/Commands.h"

#define LOCTEXT_NAMESPACE "NiagaraEditorCommands"

void FNiagaraEditorCommands::RegisterCommands()
{
	UI_COMMAND(Apply, "Apply", "Push the currently compiled script to the world.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Compile, "Compile", "Compile the current scripts", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RefreshNodes, "Refresh", "Refreshes the current graph nodes, and updates pins due to external changes.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ResetSimulation, "Reset", "Resets the current simulation", EUserInterfaceActionType::Button, FInputChord(EKeys::SpaceBar));
	UI_COMMAND(TogglePreviewGrid, "Grid", "Toggles the preview pane's grid.", EUserInterfaceActionType::ToggleButton, FInputChord() );
	UI_COMMAND(ToggleInstructionCounts, "InstructionCounts", "Display Instruction Counts", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(TogglePreviewBackground, "Background", "Toggles the preview pane's background.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleUnlockToChanges, "Lock/Unlock To Changes", "Toggles whether or not changes in the source asset get pulled into this asset automatically.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleBounds, "Bounds", "Display Bounds", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleBounds_SetFixedBounds, "Set Fixed Bounds", "Set Fixed Bounds", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleOrbit, "Orbit Mode", "Toggle Orbit Navigation", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(SaveThumbnailImage, "Thumbnail", "Generate Thumbnail", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleAutoPlay, "Auto-play", "Toggles whether or not simulations auto-play when their asset editor is opened, and when the asset is modified.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleResetSimulationOnChange, "Reset on change", "Toggles whether or not the simulation is reset whenever a change is made in the asset editor.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleResimulateOnChangeWhilePaused, "Resimulate when paused", "Toggles whether or not the simulation is rerun to the current time when making changes while paused.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(ToggleResetDependentSystems, "Reset Dependent Systems", "Toggles whether or not to reset all systems that include this emitter when it is reset by the user.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(CollapseStackToHeaders, "Collapse to Headers", "Expands all headsers and collapse all items.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::O));

	UI_COMMAND(FindInCurrentView, "Find", "Contextually finds items in current view.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::F));

	UI_COMMAND(ZoomToFit, "Zoom to Fit", "Zooms and pans to fit the current selection.", EUserInterfaceActionType::Button, FInputChord(EKeys::F));
	UI_COMMAND(ZoomToFitAll, "Zoom to Fit All", "Zooms and pans to fix all items.", EUserInterfaceActionType::Button, FInputChord(EKeys::A));
}

#undef LOCTEXT_NAMESPACE // NiagaraEditorCommands
