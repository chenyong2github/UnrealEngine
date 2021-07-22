// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

class FViewInfo;
class FRadianceCacheState;

namespace LumenRadianceCache
{
	// Must match RadianceCacheCommon.ush
	static constexpr int32 MaxClipmaps = 6;

	BEGIN_SHADER_PARAMETER_STRUCT(FRadianceCacheInputs, )
		SHADER_PARAMETER(float, ReprojectionRadiusScale)
		SHADER_PARAMETER(float, ClipmapWorldExtent)
		SHADER_PARAMETER(float, ClipmapDistributionBase)
		SHADER_PARAMETER(FIntPoint, ProbeAtlasResolutionInProbes)
		SHADER_PARAMETER(uint32, RadianceProbeClipmapResolution)
		SHADER_PARAMETER(uint32, NumRadianceProbeClipmaps)
		SHADER_PARAMETER(uint32, RadianceProbeResolution)
		SHADER_PARAMETER(uint32, FinalProbeResolution)
		SHADER_PARAMETER(uint32, FinalRadianceAtlasMaxMip)
		SHADER_PARAMETER(uint32, CalculateIrradiance)
		SHADER_PARAMETER(uint32, IrradianceProbeResolution)
		SHADER_PARAMETER(uint32, OcclusionProbeResolution)
		SHADER_PARAMETER(uint32, NumProbeTracesBudget)
	END_SHADER_PARAMETER_STRUCT()

	BEGIN_SHADER_PARAMETER_STRUCT(FRadianceCacheInterpolationParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FRadianceCacheInputs, RadianceCacheInputs)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, RadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, RadianceCacheFinalRadianceAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, RadianceCacheFinalIrradianceAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, RadianceCacheProbeOcclusionAtlas)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, RadianceCacheDepthAtlas)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ProbeWorldOffset)
		SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapTMin, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapSamplingJitter, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(float, WorldPositionToRadianceProbeCoordScale, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector3f, WorldPositionToRadianceProbeCoordBias, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(float, RadianceProbeCoordToWorldPositionScale, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector3f, RadianceProbeCoordToWorldPositionBias, [MaxClipmaps])
		SHADER_PARAMETER(FVector2D, InvProbeFinalRadianceAtlasResolution)
		SHADER_PARAMETER(FVector2D, InvProbeFinalIrradianceAtlasResolution)
		SHADER_PARAMETER(FVector2D, InvProbeDepthAtlasResolution)
		SHADER_PARAMETER(uint32, OverrideCacheOcclusionLighting)
		SHADER_PARAMETER(uint32, ShowBlackRadianceCacheLighting)
	END_SHADER_PARAMETER_STRUCT()

	void GetInterpolationParameters(
		const FViewInfo& View, 
		FRDGBuilder& GraphBuilder, 
		const FRadianceCacheState& RadianceCacheState,
		const LumenRadianceCache::FRadianceCacheInputs& RadianceCacheInputs,
		FRadianceCacheInterpolationParameters& OutParameters);
};
