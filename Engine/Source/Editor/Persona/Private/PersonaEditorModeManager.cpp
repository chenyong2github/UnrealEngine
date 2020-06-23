// Copyright Epic Games, Inc. All Rights Reserved.

#include "PersonaEditorModeManager.h"
#include "IPersonaEditMode.h"

bool FPersonaEditorModeManager::GetCameraTarget(FSphere& OutTarget) const
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		FEdMode* LegacyMode = Mode->AsLegacyMode();
		if (IPersonaEditMode* EditMode = static_cast<IPersonaEditMode*>(LegacyMode))
		{
			FSphere Target;
			if (EditMode->GetCameraTarget(Target))
			{
				OutTarget = Target;
				return true;
			}
		}
	}

	return false;
}

void FPersonaEditorModeManager::GetOnScreenDebugInfo(TArray<FText>& OutDebugText) const
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		FEdMode* LegacyMode = Mode->AsLegacyMode();
		if (IPersonaEditMode* EditMode = static_cast<IPersonaEditMode*>(LegacyMode))
		{
			EditMode->GetOnScreenDebugInfo(OutDebugText);
		}
	}
}
