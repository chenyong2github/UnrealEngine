// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementPlaceSingleTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"

constexpr TCHAR UPlacementModePlaceSingleTool::ToolName[];

bool UPlacementModeSelectAllToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UPlacementModeSelectAllToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UPlacementModePlaceSingleTool>(SceneState.ToolManager, UPlacementModePlaceSingleTool::ToolName);
}
