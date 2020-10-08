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

UInteractiveTool* UPlacementModeSelectToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UPlacementModeSelectTool>(SceneState.ToolManager, UPlacementModeSelectTool::ToolName);
}
