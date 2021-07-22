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
		SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapTMinForMark, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(float, WorldPositionToRadianceProbeCoordScaleForMark, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector3f, WorldPositionToRadianceProbeCoordBiasForMark, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(float, RadianceProbeCoordToWorldPositionScaleForMark, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector3f, RadianceProbeCoordToWorldPositionBiasForMark, [MaxClipmaps])
		SHADER_PARAMETER(uint32, RadianceProbeClipmapResolutionForMark)
		SHADER_PARAMETER(uint32, NumRadianceProbeClipmapsForMark)
	END_SHADER_PARAMETER_STRUCT()
}

DECLARE_MULTICAST_DELEGATE_ThreeParams(FMarkUsedRadianceCacheProbes, FRDGBuilder&, const FViewInfo&, const LumenRadianceCache::FRadianceCacheMarkParameters&);

extern void RenderRadianceCache(
	FRDGBuilder& GraphBuilder, 
	const FLumenCardTracingInputs& TracingInputs, 
	const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs,
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
	float DiffuseConeHalfAngle,
	int32 MaxNumProbes,
	int32 MaxProbeTraceTileResolution,
	FRDGBufferRef ProbeTraceData,
	FRDGBufferRef ProbeTraceTileData,
	FRDGBufferRef ProbeTraceTileAllocator,
	FRDGBufferRef TraceProbesIndirectArgs,
	FRDGBufferRef RadianceCacheHardwareRayTracingIndirectArgs,
	FRDGTextureUAVRef RadianceProbeAtlasTextureUAV,
	FRDGTextureUAVRef DepthProbeTextureUAV);
