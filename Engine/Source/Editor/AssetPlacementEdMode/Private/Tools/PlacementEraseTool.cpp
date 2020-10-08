// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementEraseTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"

constexpr TCHAR UPlacementModeEraseTool::ToolName[];

bool UPlacementModeEraseToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UPlacementModeEraseToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UPlacementModeEraseTool>(SceneState.ToolManager, UPlacementModeEraseTool::ToolName);
}
