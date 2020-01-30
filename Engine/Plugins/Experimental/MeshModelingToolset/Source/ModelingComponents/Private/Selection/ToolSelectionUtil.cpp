// Copyright Epic Games, Inc. All Rights Reserved.


#include "Selection/ToolSelectionUtil.h"
#include "InteractiveTool.h"
#include "InteractiveToolManager.h"
#include "GameFramework/Actor.h"

void ToolSelectionUtil::SetNewActorSelection(UInteractiveToolManager* ToolManager, AActor* Actor)
{
	FSelectedOjectsChangeList NewSelection;
	NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
	NewSelection.Actors.Add(Actor);
	ToolManager->RequestSelectionChange(NewSelection);
}