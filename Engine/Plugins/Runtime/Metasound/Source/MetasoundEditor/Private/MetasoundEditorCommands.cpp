// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorCommands.h"

#define LOCTEXT_NAMESPACE "MetasoundEditorCommands"

void FMetasoundEditorCommands::RegisterCommands()
{
	UI_COMMAND(Play, "Play", "Plays (or restarts) the Metasound", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(Stop, "Stop", "Stops Metasound (If currently playing)", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(TogglePlayback, "Toggle Playback", "Plays the Metasound or stops the currently playing Metasound", EUserInterfaceActionType::Button, FInputChord(EKeys::SpaceBar));

	UI_COMMAND(BrowserSync, "Sync to Browser", "Selects the SoundWave in the content browser", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddInput, "Add Input", "Adds an input to the node", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DeleteInput, "Delete Input", "Removes an input from the node", EUserInterfaceActionType::Button, FInputChord());
}
#undef LOCTEXT_NAMESPACE
