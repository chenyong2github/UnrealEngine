// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "BlueNoise.h"
#include "LumenRadianceCache.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"
#include "ScreenSpaceRayTracing.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "LumenSceneUtils.h"

extern int32 GLumenScreenProbeGatherNumMips;

namespace LumenScreenProbeGather 
{
	extern int32 GetTracingOctahedronResolution(const FViewInfo& View);
	extern int32 IsProbeTracingResolutionSupportedForImportanceSampling(int32 TracingResolution);
	extern bool UseImportanceSampling(const FViewInfo& View);
	extern bool UseProbeSpatialFilter();
	extern bool UseRadianceCache(const FViewInfo& View);
}

// Must match SetupAdaptiveProbeIndirectArgsCS in usf
enum class EScreenProbeIndirectArgs
{
	GroupPerProbe,
	ThreadPerProbe,
	ThreadPerTrace,
	ThreadPerGather,
	ThreadPerGatherWithBorder,
	Max
};

BEGIN_SHADER_PARAMETER_STRUCT(FScreenProbeImportanceSamplingParameters, )
	SHADER_PARAMETER(uint32, MaxImportanceSamplingOctahedronResolution)
	SHADER_PARAMETER(uint32, ScreenProbeBRDFOctahedronResolution)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, StructuredImportanceSampledRayInfosForTracing)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FScreenProbeParameters, )
	SHADER_PARAMETER(uint32, ScreenProbeTracingOctahedronResolution)
	SHADER_PARAMETER(uint32, ScreenProbeGatherOctahedronResolution)
	SHADER_PARAMETER(uint32, ScreenProbeGatherOctahedronResolutionWithBorder)
	SHADER_PARAMETER(uint32, ScreenProbeDownsampleFactor)
	SHADER_PARAMETER(FIntPoint, ScreenProbeViewSize)
	SHADER_PARAMETER(FIntPoint, ScreenProbeAtlasViewSize)
	SHADER_PARAMETER(FIntPoint, ScreenProbeAtlasBufferSize)
	SHADER_PARAMETER(float, ScreenProbeGatherMaxMip)
	SHADER_PARAMETER(float, RelativeSpeedDifferenceToConsiderLightingMoving)
	SHADER_PARAMETER(float, ScreenTraceNoFallbackThicknessScale)
	SHADER_PARAMETER(uint32, AdaptiveScreenTileSampleResolution)
	SHADER_PARAMETER(uint32, NumUniformScreenProbes)
	SHADER_PARAMETER(uint32, MaxNumAdaptiveProbes)
	SHADER_PARAMETER(int32, FixedJitterIndex)

	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, NumAdaptiveScreenProbes)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, AdaptiveScreenProbeData)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenTileAdaptiveProbeHeader)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenTileAdaptiveProbeIndices)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceRadiance)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TraceHit)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeSceneDepth)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeWorldSpeed)

	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float3>, RWTraceRadiance)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWTraceHit)

	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeImportanceSamplingParameters, ImportanceSampling)
	SHADER_PARAMETER_STRUCT_INCLUDE(FOctahedralSolidAngleParameters, OctahedralSolidAngleParameters)
	SHADER_PARAMETER_STRUCT_REF(FBlueNoise, BlueNoise)

	RDG_BUFFER_ACCESS(ProbeIndirectArgs, ERHIAccess::IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FScreenProbeGatherParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeRadiance)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenProbeRadianceWithBorder)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float3>, ScreenProbeRadianceSHAmbient)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float3>, ScreenProbeRadianceSHDirectional)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, ScreenProbeMoving)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FCompactedTraceParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, CompactedTraceTexelData)
	RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

extern void GenerateBRDF_PDF(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	FRDGTextureRef& BRDFProbabilityDensityFunction,
	FRDGBufferSRVRef& BRDFProbabilityDensityFunctionSH,
	FScreenProbeParameters& ScreenProbeParameters);

extern void GenerateImportanceSamplingRays(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FRDGTextureRef BRDFProbabilityDensityFunction,
	FRDGBufferSRVRef BRDFProbabilityDensityFunctionSH,
	FScreenProbeParameters& ScreenProbeParameters);

extern void TraceScreenProbes(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	bool bTraceMeshSDFs,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const ScreenSpaceRayTracing::FPrevSceneColorMip& PrevSceneColor,
	FRDGTextureRef LightingChannelsTexture,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FScreenProbeParameters& ScreenProbeParameters,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters);

void RenderHardwareRayTracingScreenProbe(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	FScreenProbeParameters& CommonDiffuseParameters,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenIndirectTracingParameters& DiffuseTracingParameters,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	const FCompactedTraceParameters& CompactedTraceParameters);

extern void FilterScreenProbes(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FScreenProbeParameters& ScreenProbeParameters,
	FScreenProbeGatherParameters& GatherParameters);

BEGIN_SHADER_PARAMETER_STRUCT(FScreenSpaceBentNormalParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, ScreenBentNormal)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, ScreenDiffuseLighting)
	SHADER_PARAMETER(uint32, UseScreenBentNormal)
END_SHADER_PARAMETER_STRUCT()

extern FScreenSpaceBentNormalParameters ComputeScreenSpaceBentNormal(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FMinimalSceneTextures& SceneTextures,
	FRDGTextureRef LightingChannelsTexture,
	const FScreenProbeParameters& ScreenProbeParameters);

namespace LumenScreenProbeGatherRadianceCache
{
	LumenRadianceCache::FRadianceCacheInputs SetupRadianceCacheInputs();
}