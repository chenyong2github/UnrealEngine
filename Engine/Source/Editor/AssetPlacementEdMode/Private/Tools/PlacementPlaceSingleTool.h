// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "Tools/PlacementClickDragToolBase.h"

#include "PlacementPlaceSingleTool.generated.h"

UCLASS(Transient, MinimalAPI)
class UPlacementModePlaceSingleToolBuilder : public UPlacementToolBuilderBase
{
	GENERATED_BODY()

protected:
	virtual UPlacementBrushToolBase* FactoryToolInstance(UObject* Outer) const override;
};

UCLASS(MinimalAPI)
class UPlacementModePlaceSingleTool : public UPlacementClickDragToolBase
{
	GENERATED_BODY()

public:
	constexpr static TCHAR ToolName[] = TEXT("PlaceSingleTool");

	virtual void OnEndDrag(const FRay& Ray) override;
};
