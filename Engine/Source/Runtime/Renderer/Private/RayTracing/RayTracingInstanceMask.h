// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RayTracingInstance.h"

#if RHI_RAYTRACING

/** Compute the mask based on blend mode for ray tracing*/
RENDERER_API uint8 BlendModeToRayTracingInstanceMask(const EBlendMode BlendMode);

/** Build the Instance mask and flags in renderer module. */
FRayTracingMaskAndFlags BuildRayTracingInstanceMaskAndFlags(const FRayTracingInstance& Instance, const FPrimitiveSceneProxy& PrimitiveSceneProxy);

FORCEINLINE void UpdateRayTracingInstanceMaskAndFlagsIfNeeded(FRayTracingInstance& Instance, const FPrimitiveSceneProxy& PrimitiveSceneProxy, bool bForceUpdate = false)
{

	if (Instance.GetMaterials().IsEmpty()) 
	{
		// If the material list is empty, explicitly set the mask to 0 so it will not be added in the raytracing scene
		Instance.Mask = 0; 
		return; 
	}

	if (Instance.bInstanceMaskAndFlagsDirty || bForceUpdate)
	{
		FRayTracingMaskAndFlags  MaskAndFlags = BuildRayTracingInstanceMaskAndFlags(Instance, PrimitiveSceneProxy);

		Instance.Mask = MaskAndFlags.Mask;
		Instance.bForceOpaque = MaskAndFlags.bForceOpaque;
		Instance.bDoubleSided = MaskAndFlags.bDoubleSided;

		// Clean the dirty bit
		Instance.bInstanceMaskAndFlagsDirty = false;
	}
}
#endif