// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

/**
* Class containing commands for curve viewer actions
*/
class FCurveViewerCommands : public TCommands<FCurveViewerCommands>
{
public:
	FCurveViewerCommands()
	: TCommands<FCurveViewerCommands>
		(
			TEXT("CurveViewer"), // Context name for fast lookup
			NSLOCTEXT("Contexts", "CurveViewer", "Curve Viewer"), // Localized context name for displaying
			NAME_None, // Parent context name.  
			FAppStyle::GetAppStyleSetName() // Icon Style Set
		)
	{}

	/** Initialize commands */
	virtual void RegisterCommands() override;

	/** Add curve */
	TSharedPtr< FUICommandInfo > AddCurve;
};


