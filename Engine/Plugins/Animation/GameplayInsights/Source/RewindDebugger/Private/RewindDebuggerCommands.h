// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/Commands.h"

#define LOCTEXT_NAMESPACE "FRewindDebuggerCommands"

class FRewindDebuggerCommands : public TCommands<FRewindDebuggerCommands>
{
public:

	/** Default constructor. */
	FRewindDebuggerCommands()
		: TCommands<FRewindDebuggerCommands>("RewindDebugger", NSLOCTEXT("Contexts", "RewindDebugger", "Animation Insights 2"), NAME_None, "RewindDebuggerStyle")
	{ }

	// TCommands interface

	virtual void RegisterCommands() override
	{
		UI_COMMAND(StartRecording, "Start Recording", "Start recording Animation Insights data", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(StopRecording, "Stop Recording", "Stop recording Animation Insights data", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(FirstFrame, "First Frame", "Jump to first recorded frame", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(PreviousFrame, "Previous Frame", "Step one frame back", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ReversePlay, "Reverse Play", "Playback recorded data in reverse", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(Pause, "Pause", "Pause playback", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(Play, "Play", "Playback recorded data", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(NextFrame, "Next Frame", "Step one frame forward", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(LastFrame, "Last Frame", "Jump to last recorded frame", EUserInterfaceActionType::Button, FInputChord());
	}


	TSharedPtr<FUICommandInfo> StartRecording;
	TSharedPtr<FUICommandInfo> StopRecording;
	TSharedPtr<FUICommandInfo> FirstFrame;
	TSharedPtr<FUICommandInfo> PreviousFrame;
	TSharedPtr<FUICommandInfo> ReversePlay;
	TSharedPtr<FUICommandInfo> Pause;
	TSharedPtr<FUICommandInfo> Play;
	TSharedPtr<FUICommandInfo> NextFrame;
	TSharedPtr<FUICommandInfo> LastFrame;
};


#undef LOCTEXT_NAMESPACE
