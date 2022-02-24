// Copyright Epic Games, Inc. All Rights Reserved.

#include "PersonaEditorModeManager.h"
#include "IPersonaEditMode.h"
#include "IPersonaPreviewScene.h"
#include "Selection.h"
#include "Animation/DebugSkelMeshComponent.h"


bool FPersonaEditorModeManager::GetCameraTarget(FSphere& OutTarget) const
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		FEdMode* LegacyMode = Mode->AsLegacyMode();
		// Hack for UE-136071, UE-141936. ClothPaintModes are not IPersonaEditModes, but are FEdModes.
		if (LegacyMode && LegacyMode->GetID() != FEditorModeID("ClothPaintMode"))
		{
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
	}

	return false;
}

void FPersonaEditorModeManager::GetOnScreenDebugInfo(TArray<FText>& OutDebugText) const
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		FEdMode* LegacyMode = Mode->AsLegacyMode();
		// Hack for UE-136071, UE-141936. ClothPaintModes are not IPersonaEditModes, but are FEdModes.
		if (LegacyMode && LegacyMode->GetID() != FEditorModeID("ClothPaintMode"))
		{
			if (IPersonaEditMode* EditMode = static_cast<IPersonaEditMode*>(LegacyMode))
			{
				EditMode->GetOnScreenDebugInfo(OutDebugText);
			}
		}
	}
}


void FPersonaEditorModeManager::SetPreviewScene(FPreviewScene* NewPreviewScene)
{
	const IPersonaPreviewScene *PersonaPreviewScene = static_cast<const IPersonaPreviewScene *>(NewPreviewScene);

	if (PersonaPreviewScene && PersonaPreviewScene->GetPreviewMeshComponent())
	{
		ComponentSet->BeginBatchSelectOperation();
		ComponentSet->DeselectAll();
		ComponentSet->Select(PersonaPreviewScene->GetPreviewMeshComponent(), true);
		ComponentSet->EndBatchSelectOperation();
	}

	FAssetEditorModeManager::SetPreviewScene(NewPreviewScene);
}
