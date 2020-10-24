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
	 * This is a hack. When we are inside the ::GetDynamicMeshElements() of a UGizmoBaseComponent RenderProxy, we need 
	 * to draw to a set of FSceneView and we do not know which one is focused. UGizmoBaseComponent implementations generally
	 * try to adapt their rendering to the current view, and some of that (currently) can only happen at the
	 * level of the RenderProxy. So we need to tell the Component which FSceneView has focus, and we do that
	 * by calling this function from the EdMode.
	 */
	INTERACTIVETOOLSFRAMEWORK_API void SetGlobalFocusedEditorSceneView(const FSceneView* View);

	/**
	 * This function searches the input Views list for the global currently-focused FSceneView that was last set in
	 * SetGlobalFocusedEditorSceneView(). Note that the Views pointers are different every frame, and 
	 * we are calling SetGlobalFocusedEditorSceneView() on the Game thread but this function from the Render Thread.
	 * So it is entirely possible that we will "miss" our FSceneView and this function will return NULL.
	 * Implementations need to handle this case.
	 */
	INTERACTIVETOOLSFRAMEWORK_API const FSceneView* FindFocusedEditorSceneView(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap);


	/**
	 * Turn on/off the above global focused scene view tracking. When disabled, FindFocusedEditorSceneView()
	 * will return the first visible FSceneView, or the first element of the Views array otherwise.
	 * Enabled by default in Editor builds, disabled by default in Runtime builds.
	 */
	INTERACTIVETOOLSFRAMEWORK_API void SetGlobalFocusedSceneViewTrackingEnabled(bool bEnabled);


	/**
	 * @return Conversion factor between pixel and world-space coordinates at 3D point Location in View.
	 * @warning This is a local estimate and is increasingly incorrect as the 3D point gets further from Location
	 */
	INTERACTIVETOOLSFRAMEWORK_API float CalculateLocalPixelToWorldScale(
		const FSceneView* View,
		const FVector& Location);

}
