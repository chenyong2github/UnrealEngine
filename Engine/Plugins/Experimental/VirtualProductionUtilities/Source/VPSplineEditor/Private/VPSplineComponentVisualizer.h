// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "HitProxies.h"
#include "InputCoreTypes.h"

#include "ComponentVisualizer.h"
#include "SplineComponentVisualizer.h"

#include "Components/SplineComponent.h"
#include "VPSplineComponent.h"


class FVPSplineComponentVisualizer : public FSplineComponentVisualizer
{
public:
	FVPSplineComponentVisualizer();
	virtual ~FVPSplineComponentVisualizer();

	//~ Begin FComponentVisualizer Interface
	virtual void OnRegister() override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;

};