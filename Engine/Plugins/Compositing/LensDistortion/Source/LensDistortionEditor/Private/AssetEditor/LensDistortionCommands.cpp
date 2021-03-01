// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionCommands.h"

#include "Framework/Commands/Commands.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Commands/UICommandInfo.h"



#define LOCTEXT_NAMESPACE "LensDistortionCommands"

void FLensDistortionCommands::RegisterCommands()
{
	UI_COMMAND(Edit, "Edit", "Edit the current lens file.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE