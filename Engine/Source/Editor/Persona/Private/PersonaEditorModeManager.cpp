// Copyright Epic Games, Inc. All Rights Reserved.

#include "PersonaEditorModeManager.h"
#include "IPersonaEditMode.h"

bool FPersonaEditorModeManager::GetCameraTarget(FSphere& OutTarget) const
{
	// Note: assumes all of our modes are IPersonaEditMode!
	for(int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		TSharedPtr<IPersonaEditMode> EditMode = StaticCastSharedPtr<IPersonaEditMode>(ActiveModes[ModeIndex]);

		FSphere Target;
		if (EditMode->GetCameraTarget(Target))
		{
			OutTarget = Target;
			return true;
		}
	}

	return false;
}

void FPersonaEditorModeManager::GetOnScreenDebugInfo(TArray<FText>& OutDebugText) const
{
	// Note: assumes all of our modes are IPersonaEditMode!
	for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
	{
		TSharedPtr<IPersonaEditMode> EditMode = StaticCastSharedPtr<IPersonaEditMode>(ActiveModes[ModeIndex]);
		EditMode->GetOnScreenDebugInfo(OutDebugText);
	}
}
