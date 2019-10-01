// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

/*-----------------------------------------------------------------------------
   FSoundCueGraphEditorCommands
-----------------------------------------------------------------------------*/

class FSoundClassEditorCommands : public TCommands<FSoundClassEditorCommands>
{
public:
	/** Constructor */
	FSoundClassEditorCommands()
		: TCommands<FSoundClassEditorCommands>("SoundClassEditor", NSLOCTEXT("Contexts", "SoundClassEditor", "SoundClass Editor"), NAME_None, FEditorStyle::GetStyleSetName())
	{
	}
	
	/** Plays the SoundCue or stops the currently playing cue/node */
	TSharedPtr<FUICommandInfo> ToggleSolo;

	/** Plays the SoundCue or stops the currently playing cue/node */
	TSharedPtr<FUICommandInfo> ToggleMute;

	/** Initialize commands */
	virtual void RegisterCommands() override;
};
