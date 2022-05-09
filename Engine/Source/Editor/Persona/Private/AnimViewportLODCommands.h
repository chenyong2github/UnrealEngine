// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

/**
 * Class containing commands for persona viewport LOD actions
 */
class FAnimViewportLODCommands : public TCommands<FAnimViewportLODCommands>
{
public:
	FAnimViewportLODCommands() 
		: TCommands<FAnimViewportLODCommands>
		(
			TEXT("AnimViewportLODCmd"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "AnimViewportLODCmd", "Animation Viewport LOD Command"), // Localized context name for displaying
			NAME_None, // Parent context name. 
			FAppStyle::GetAppStyleSetName() // Icon Style Set
		)
	{
	}

	/** LOD Debug */
	TSharedPtr< FUICommandInfo > LODDebug;

	/** LOD Auto */
	TSharedPtr< FUICommandInfo > LODAuto;

	/** LOD 0 */
	TSharedPtr< FUICommandInfo > LOD0;

public:
	/** Registers our commands with the binding system */
	virtual void RegisterCommands() override;
};
