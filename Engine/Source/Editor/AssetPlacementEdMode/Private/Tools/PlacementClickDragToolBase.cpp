// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementClickDragToolBase.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"
#include "BaseGizmos/BrushStampIndicator.h"

void UPlacementClickDragToolBase::Setup()
{
	UPlacementBrushToolBase::Setup();

	RemoveToolPropertySource(BrushProperties);
	BrushStampIndicator->bDrawRadiusCircle = false;
	BrushProperties->BrushSize = .5f;
	BrushProperties->BrushFalloffAmount = 1.f;
}

double UPlacementClickDragToolBase::EstimateMaximumTargetDimension()
{
	return UBaseBrushTool::EstimateMaximumTargetDimension();
}