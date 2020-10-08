// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementReapplySettingsTool.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"

constexpr TCHAR UPlacementModeReapplySettingsTool::ToolName[];

bool UPlacementModeReapplySettingsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UPlacementModeReapplySettingsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UPlacementModeReapplySettingsTool>(SceneState.ToolManager, UPlacementModeReapplySettingsTool::ToolName);
}
