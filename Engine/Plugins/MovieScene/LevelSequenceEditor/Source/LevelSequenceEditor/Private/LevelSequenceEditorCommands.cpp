// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceEditorCommands.h"

#define LOCTEXT_NAMESPACE "LevelSequenceEditorCommands"

FLevelSequenceEditorCommands::FLevelSequenceEditorCommands()
	: TCommands<FLevelSequenceEditorCommands>("LevelSequenceEditor", LOCTEXT("LevelSequenceEditor", "Level Sequence Editor"), NAME_None, "LevelSequenceEditorStyle")
{
}

void FLevelSequenceEditorCommands::RegisterCommands()
{
	UI_COMMAND(CreateNewLevelSequenceInLevel, "Add Level Sequence", "Create a new level sequence asset, and place an instance of it in this level", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CreateNewMasterSequenceInLevel, "Add Master Sequence", "Create a new master sequence asset, and place an instance of it in this level", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleCinematicViewportCommand, "Cinematic Viewport", "A viewport layout tailored to cinematic preview", EUserInterfaceActionType::RadioButton, FInputChord());

	UI_COMMAND(BakeTransform, "Bake Transform", "Bake transform in world space, removing any existing transform and attach tracks", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(FixActorReferences, "Fix Actor References", "Try to automatically fix up broken actor bindings", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
