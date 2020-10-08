// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "Tools/PlacementBrushToolBase.h"

#include "PlacementEraseTool.generated.h"

UCLASS(Transient, MinimalAPI)
class UPlacementModeEraseToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UCLASS(MinimalAPI)
class UPlacementModeEraseTool : public UPlacementBrushToolBase
{
	GENERATED_BODY()
public:
	constexpr static TCHAR ToolName[] = TEXT("EraseTool");
};
