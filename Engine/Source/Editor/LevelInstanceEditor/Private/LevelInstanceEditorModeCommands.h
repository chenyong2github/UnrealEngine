// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FLevelInstanceEditorModeCommands : public TCommands<FLevelInstanceEditorModeCommands>
{

public:
	FLevelInstanceEditorModeCommands() : TCommands<FLevelInstanceEditorModeCommands>
		(
			"LevelInstanceEditorMode",
			NSLOCTEXT("Contexts", "LevelInstanceEditor", "Level Instance Editor"),
			NAME_None,
			FAppStyle::GetAppStyleSetName() // Icon Style Set
		)
	{}


	/** Exit Editor Mode */
	TSharedPtr<FUICommandInfo> ExitMode;

	/** Toggle Restrict Selection */
	TSharedPtr<FUICommandInfo> ToggleContextRestriction;
		
	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;
};