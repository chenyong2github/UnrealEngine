// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StereoLayerComponentVisualizer.h"
#include "SceneManagement.h"

#include "Components/StereoLayerComponent.h"


void FStereoLayerComponentVisualizer::DrawVisualization( const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI )
{
	const UStereoLayerComponent* StereoLayerComp = Cast<const UStereoLayerComponent>(Component);
	if(StereoLayerComp != nullptr)
	{
		check(StereoLayerComp->Shape != nullptr);
		StereoLayerComp->Shape->DrawShapeVisualization(View, PDI);
    }
}
