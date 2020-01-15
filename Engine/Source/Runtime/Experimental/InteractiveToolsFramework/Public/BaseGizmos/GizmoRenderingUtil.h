// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"


/**
 * Utility functions for standard GizmoComponent rendering
 */
namespace GizmoRenderingUtil
{
	/**
	 * @return best guess at "active" scene view. Prefers Perspective view if available,
	 * otherwise first valid view.
	 */
	INTERACTIVETOOLSFRAMEWORK_API const FSceneView* FindActiveSceneView(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily, 
		uint32 VisibilityMap);

	/**
	 * @return Conversion factor between pixel and world-space coordinates at 3D point Location in View.
	 * @warning This is a local estimate and is increasingly incorrect as the 3D point gets further from Location
	 */
	INTERACTIVETOOLSFRAMEWORK_API float CalculateLocalPixelToWorldScale(
		const FSceneView* View,
		const FVector& Location);

}
