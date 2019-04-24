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
extern bool ShouldRenderRayTracingAmbientOcclusion();
extern bool ShouldRenderRayTracingReflections(const TArray<FViewInfo>& Views);
extern bool ShouldRenderRayTracingGlobalIllumination(const TArray<FViewInfo>& Views);
extern bool ShouldRenderRayTracingTranslucency(const TArray<FViewInfo>& Views);
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

FORCEINLINE bool ShouldRenderRayTracingAmbientOcclusion()
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingReflections(const TArray<FViewInfo>& Views)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingGlobalIllumination(const TArray<FViewInfo>& Views)
{
	return false;
}

FORCEINLINE bool ShouldRenderRayTracingTranslucency(const TArray<FViewInfo>& Views)
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
