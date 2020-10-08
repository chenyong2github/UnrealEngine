// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "Tools/PlacementBrushToolBase.h"

#include "PlacementReapplySettingsTool.generated.h"

UCLASS(Transient, MinimalAPI)
class UPlacementModeReapplySettingsToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UCLASS(MinimalAPI)
class UPlacementModeReapplySettingsTool : public UPlacementBrushToolBase
{
	GENERATED_BODY()
public:
	constexpr static TCHAR ToolName[] = TEXT("ReapplySettings");
};
