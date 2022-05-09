// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Styling/AppStyle.h"
#include "Framework/Commands/Commands.h"

/**
 * Defines commands for the anim sequence curve editor
 */
class FAnimSequenceCurveEditorCommands : public TCommands<FAnimSequenceCurveEditorCommands>
{
public:
	FAnimSequenceCurveEditorCommands()
		: TCommands<FAnimSequenceCurveEditorCommands>
		(
			TEXT("AnimSequenceCurveEditor"),
			NSLOCTEXT("Contexts", "AnimSequenceCurveEditor", "Anim Sequence Curve Editor"),
			NAME_None,
			FAppStyle::GetAppStyleSetName()
		)
	{
	}

	TSharedPtr<FUICommandInfo> EditSelectedCurves;

public:
	virtual void RegisterCommands() override;
};
