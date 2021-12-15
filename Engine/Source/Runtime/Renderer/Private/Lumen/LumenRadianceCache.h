// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LumenRadianceCacheInterpolation.h"

class FLumenCardTracingInputs;
class FSceneTextureParameters;
class FScreenProbeParameters;

namespace LumenRadianceCache
{
	BEGIN_SHADER_PARAMETER_STRUCT(FRadianceCacheMarkParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWRadianceProbeIndirectionTexture)
		SHADER_PARAMETER_ARRAY(FVector4f, PackedWorldPositionToRadianceProbeCoord, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector4f, PackedRadianceProbeCoordToWorldPosition, [MaxClipmaps])
		SHADER_PARAMETER(uint32, RadianceProbeClipmapResolutionForMark)
		SHADER_PARAMETER(uint32, NumRadianceProbeClipmapsForMark)
		SHADER_PARAMETER(float, InvClipmapFadeSizeForMark)
	END_SHADER_PARAMETER_STRUCT()
}

inline void SetWorldPositionToRadianceProbeCoord(FVector4f& PackedParams, const FVector3f& BiasForMark, const float ScaleForMark)
{
	PackedParams = FVector4f(BiasForMark, ScaleForMark);
}

inline void SetRadianceProbeCoordToWorldPosition(FVector4f& PackedParams, const FVector3f& BiasForMark, const float ScaleForMark)
{
	PackedParams = FVector4f(BiasForMark, ScaleForMark);
}

DECLARE_MULTICAST_DELEGATE_ThreeParams(FMarkUsedRadianceCacheProbes, FRDGBuilder&, const FViewInfo&, const LumenRadianceCache::FRadianceCacheMarkParameters&);

struct FRadianceCacheConfiguration
{
	bool bFarField = true;
};

extern void RenderRadianceCache(
	FRDGBuilder& GraphBuilder, 
	const FLumenCardTracingInputs& TracingInputs, 
	const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs,
	FRadianceCacheConfiguration Configuration,
	const class FScene* Scene,
	const FViewInfo& View, 
	const FScreenProbeParameters* ScreenProbeParameters,
	FRDGBufferSRVRef BRDFProbabilityDensityFunctionSH,
	FMarkUsedRadianceCacheProbes MarkUsedRadianceCacheProbes,
	FRadianceCacheState& RadianceCacheState,
	LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters);

extern void RenderLumenHardwareRayTracingRadianceCache(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FRadianceCacheConfiguration Configuration,
	float DiffuseConeHalfAngle,
	int32 MaxNumProbes,
	int32 MaxProbeTraceTileResolution,
	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef TraceProbesIndirectArgs,
	FRDGBufferRef HardwareRayTracingRayAllocatorBuffer,
	FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs,
	FRDGTextureUAVRef RadianceProbeAtlasTextureUAV,
	FRDGTextureUAVRef DepthProbeTextureUAV);

extern void MarkUsedProbesForVisualize(FRDGBuilder& GraphBuilder, const FViewInfo& View, const class LumenRadianceCache::FRadianceCacheMarkParameters& RadianceCacheMarkParameters);