// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"

#include "UI/LensDistortionEditorStyle.h"


class FUICommandInfo;


class FLensDistortionCommands : public TCommands<FLensDistortionCommands>
{
public:

	FLensDistortionCommands()
		: TCommands<FLensDistortionCommands>(TEXT("LensDistortion"), NSLOCTEXT("LensDistortionCommands", "LensDistortionCommandsEdit", "LensDistortion"), NAME_None, FLensDistortionEditorStyle::GetStyleSetName())
	{}
	
	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;

	/** Edit the current lens distortion. */
	TSharedPtr<FUICommandInfo> Edit;
};
