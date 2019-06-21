// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RaytracingOptions.h declares ray tracing options for use in rendering
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"

class FSkyLightSceneProxy;
class FViewInfo;
class FLightSceneInfoCompact;
class FLightSceneInfo;


#if RHI_RAYTRACING
extern int32 GetForceRayTracingEffectsCVarValue();

extern bool ShouldRenderRayTracingSkyLight(const FSkyLightSceneProxy* SkyLightSceneProxy);
extern bool ShouldRenderRayTracingAmbientOcclusion(const FViewInfo& View);
extern bool ShouldRenderRayTracingReflections(const FViewInfo& View);
extern bool ShouldRenderRayTracingGlobalIllumination(const FViewInfo& View);
extern bool ShouldRenderRayTracingTranslucency(const FViewInfo& View);
extern bool ShouldRenderRayTracingShadows(const FLightSceneProxy& LightProxy);
extern bool ShouldRenderRayTracingShadows(const FLightSceneInfoCompact& LightInfo);
extern bool ShouldRenderRayTracingStochasticRectLight(const FLightSceneInfo& LightInfo);
extern bool CanOverlayRayTracingOutput(const FViewInfo& View);

extern float GetRaytracingMaxNormalBias();

#else

FORCEINLINE int32 GetForceRayTracingEffectsCVarValue()
{
	return 0;
}

FORCEINLINE bool ShouldRenderRayTracingSkyLight(const FSkyLightSceneProxy* SkyLightSceneProxy)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingAmbientOcclusion(const FViewInfo& View)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingReflections(const FViewInfo& View)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingGlobalIllumination(const FViewInfo& View)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingTranslucency(const FViewInfo& View)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingShadows(const FLightSceneProxy& LightProxy)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingShadows(const FLightSceneInfoCompact& LightInfo)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingStochasticRectLight(const FLightSceneInfo& LightInfo)
{
	return false;
}

FORCEINLINE bool CanOverlayRayTracingOutput(const FViewInfo& View)
{
	return true;
}

#endif
