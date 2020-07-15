// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

class FMetasoundEditorCommands : public TCommands<FMetasoundEditorCommands>
{
public:
	/** Constructor */
	FMetasoundEditorCommands() 
		: TCommands<FMetasoundEditorCommands>("MetasoundEditor", NSLOCTEXT("Contexts", "MetasoundEditor", "Metasound Graph Editor"), NAME_None, "MetasoundStyle")
	{
	}
	
	/** Plays the Metasound */
	TSharedPtr<FUICommandInfo> Play;
	
	/** Stops the currently playing Metasound */
	TSharedPtr<FUICommandInfo> Stop;

	/** Plays stops the currently playing Metasound */
	TSharedPtr<FUICommandInfo> TogglePlayback;

	/** Selects the SoundWave in the content browser */
	TSharedPtr<FUICommandInfo> BrowserSync;

	/** Breaks the node input/output link */
	TSharedPtr<FUICommandInfo> BreakLink;

	/** Adds an input to the node */
	TSharedPtr<FUICommandInfo> AddInput;

	/** Removes an input from the node */
	TSharedPtr<FUICommandInfo> DeleteInput;

	/** Initialize commands */
	virtual void RegisterCommands() override;
};
