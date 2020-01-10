// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

/**
* Class containing commands for curve viewer actions
*/
class FCurveContainerCommands : public TCommands<FCurveContainerCommands>
{
public:
	FCurveContainerCommands()
	: TCommands<FCurveContainerCommands>
		(
			TEXT("CurveContainer"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "CurveContainer", "Curve Viewer"), // Localized context name for displaying
			NAME_None, // Parent context name.  
			FEditorStyle::GetStyleSetName() // Icon Style Set
		)
	{}

	/** Initialize commands */
	virtual void RegisterCommands() override;

	/** Add curve */
	TSharedPtr< FUICommandInfo > AddCurve;
};


