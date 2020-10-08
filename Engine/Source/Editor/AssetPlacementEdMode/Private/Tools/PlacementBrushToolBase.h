// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/BaseBrushTool.h"

#include "PlacementBrushToolBase.generated.h"

class FRay;
struct FHitResult;

UCLASS(Abstract, MinimalAPI)
class UPlacementBrushToolBase : public UBaseBrushTool
{
	GENERATED_BODY()
	
public:
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
};
