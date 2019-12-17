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

// be sure to also update the definition in the `RayTracingPrimaryRays.usf`
enum class ERayTracingPrimaryRaysFlag: uint32 
{
	None                      =      0,
	UseGBufferForMaxDistance  = 1 << 0,
	ConsiderSurfaceScatter	  = 1 << 1,
	AllowSkipSkySample		  = 1 << 2,
};

ENUM_CLASS_FLAGS(ERayTracingPrimaryRaysFlag);

struct FRayTracingPrimaryRaysOptions
{
	int32 IsEnabled;
	int32 SamplerPerPixel;
	int32 ApplyHeightFog;
	float PrimaryRayBias;
	float MaxRoughness;
	int32 MaxRefractionRays;
	int32 EnableEmmissiveAndIndirectLighting;
	int32 EnableDirectLighting;
	int32 EnableShadows;
	float MinRayDistance;
	float MaxRayDistance;
	int32 EnableRefraction;
};


#if RHI_RAYTRACING
extern bool AnyRayTracingPassEnabled(const FViewInfo& View);
extern int32 GetForceRayTracingEffectsCVarValue();
extern FRayTracingPrimaryRaysOptions GetRayTracingTranslucencyOptions();

extern bool ShouldRenderRayTracingSkyLight(const FSkyLightSceneProxy* SkyLightSceneProxy);
extern bool ShouldRenderRayTracingAmbientOcclusion(const FViewInfo& View);
extern bool ShouldRenderRayTracingReflections(const FViewInfo& View);
extern bool ShouldRenderRayTracingGlobalIllumination(const FViewInfo& View);
extern bool ShouldRenderRayTracingTranslucency(const FViewInfo& View);
extern bool ShouldRenderRayTracingShadows(const FLightSceneProxy& LightProxy);
extern bool ShouldRenderRayTracingShadows(const FLightSceneInfoCompact& LightInfo);
extern bool ShouldRenderRayTracingStochasticRectLight(const FLightSceneInfo& LightInfo);
extern bool CanOverlayRayTracingOutput(const FViewInfo& View);

extern bool EnableRayTracingShadowTwoSidedGeometry();
extern float GetRaytracingMaxNormalBias();

extern bool CanUseRayTracingLightingMissShader(EShaderPlatform ShaderPlatform);

#else

FORCEINLINE bool AnyRayTracingPassEnabled(const FViewInfo& View)
{
	return 0;
}

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

FORCEINLINE bool CanUseRayTracingLightingMissShader(EShaderPlatform)
{
	return false;
}

#endif
