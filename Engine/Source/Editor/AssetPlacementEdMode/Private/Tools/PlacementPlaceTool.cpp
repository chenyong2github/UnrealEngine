// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementPlaceTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"

constexpr TCHAR UPlacementModePlacementTool::ToolName[];

bool UPlacementModePlacementToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UPlacementModePlacementToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UPlacementModePlacementTool>(SceneState.ToolManager, UPlacementModePlacementTool::ToolName);
}
