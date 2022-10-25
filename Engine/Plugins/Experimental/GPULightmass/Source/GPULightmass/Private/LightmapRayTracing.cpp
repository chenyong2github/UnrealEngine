// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightmapRayTracing.h"

bool IsSupportedVertexFactoryType(const FVertexFactoryType* VertexFactoryType)
{
	return VertexFactoryType->SupportsLightmapBaking();
}

#if RHI_RAYTRACING

IMPLEMENT_GLOBAL_RAYTRACING_SHADER(FLightmapPathTracingRGS, "/Plugin/GPULightmass/Private/LightmapPathTracing.usf", "LightmapPathTracingMainRG", SF_RayGen, ERayTracingPayloadType::Minimal | ERayTracingPayloadType::PathTracingMaterial);
IMPLEMENT_GLOBAL_RAYTRACING_SHADER(FVolumetricLightmapPathTracingRGS, "/Plugin/GPULightmass/Private/LightmapPathTracing.usf", "VolumetricLightmapPathTracingMainRG", SF_RayGen, ERayTracingPayloadType::Minimal | ERayTracingPayloadType::PathTracingMaterial);
IMPLEMENT_GLOBAL_RAYTRACING_SHADER(FStationaryLightShadowTracingRGS, "/Plugin/GPULightmass/Private/LightmapPathTracing.usf", "StationaryLightShadowTracingMainRG", SF_RayGen, ERayTracingPayloadType::Minimal | ERayTracingPayloadType::PathTracingMaterial);
IMPLEMENT_GLOBAL_RAYTRACING_SHADER(FStaticShadowDepthMapTracingRGS, "/Plugin/GPULightmass/Private/StaticShadowDepthMap.usf", "StaticShadowDepthMapTracingRG", SF_RayGen, ERayTracingPayloadType::Minimal | ERayTracingPayloadType::PathTracingMaterial);

IMPLEMENT_GLOBAL_SHADER(FFirstBounceRayGuidingCDFBuildCS, "/Plugin/GPULightmass/Private/FirstBounceRayGuidingCDFBuild.usf", "FirstBounceRayGuidingCDFBuildCS", SF_Compute);

#endif
