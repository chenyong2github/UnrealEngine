// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementLassoSelectTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"

constexpr TCHAR UPlacementModeLassoSelectTool::ToolName[];

bool UPlacementModeLassoSelectToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UPlacementModeLassoSelectToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UPlacementModeLassoSelectTool>(SceneState.ToolManager, UPlacementModeLassoSelectTool::ToolName);
}
