// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "Tools/PlacementClickDragToolBase.h"

#include "PlacementSelectTool.generated.h"

UCLASS(Transient, MinimalAPI)
class UPlacementModeSelectToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UCLASS(MinimalAPI)
class UPlacementModeSelectTool : public UPlacementClickDragToolBase
{
	GENERATED_BODY()

public:
	constexpr static TCHAR ToolName[] = TEXT("SelectTool");
};
