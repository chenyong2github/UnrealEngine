// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementSelectTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"

constexpr TCHAR UPlacementModeSelectTool::ToolName[];

bool UPlacementModeSelectToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UPlacementBrushToolBase* UPlacementModeSelectToolBuilder::FactoryToolInstance(UObject* Outer) const
{
	return NewObject<UPlacementModeSelectTool>(Outer);
}

void UPlacementModeSelectTool::Setup()
{
	Super::Setup();
	ShutdownBrushStampIndicator();
}

FInputRayHit UPlacementModeSelectTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	return FInputRayHit();	// always fall-through to the EdMode or viewport handling for clicks.
}
