// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

class FViewInfo;

namespace LumenRadianceCache
{
	// Must match RadianceCacheCommon.ush
	static constexpr int32 MaxClipmaps = 6;

	BEGIN_SHADER_PARAMETER_STRUCT(FRadianceCacheParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, RadianceProbeIndirectionTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RadianceCacheFinalRadianceAtlas)
		SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapTMin, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(float, RadianceProbeClipmapSamplingJitter, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(float, WorldPositionToRadianceProbeCoordScale, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector, WorldPositionToRadianceProbeCoordBias, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(float, RadianceProbeCoordToWorldPositionScale, [MaxClipmaps])
		SHADER_PARAMETER_ARRAY(FVector, RadianceProbeCoordToWorldPositionBias, [MaxClipmaps])
		SHADER_PARAMETER(float, ReprojectionRadiusScale)
		SHADER_PARAMETER(float, FinalRadianceAtlasMaxMip)
		SHADER_PARAMETER(FVector2D, InvRadianceProbeAtlasResolution)
		SHADER_PARAMETER(FIntPoint, ProbeAtlasResolutionInProbes)
		SHADER_PARAMETER(uint32, RadianceProbeClipmapResolution)
		SHADER_PARAMETER(uint32, NumRadianceProbeClipmaps)
		SHADER_PARAMETER(uint32, RadianceProbeResolution)
		SHADER_PARAMETER(uint32, FinalProbeResolution)
		SHADER_PARAMETER(uint32, OverrideCacheOcclusionLighting)
		SHADER_PARAMETER(uint32, ShowBlackRadianceCacheLighting)
	END_SHADER_PARAMETER_STRUCT()

	void GetParameters(const FViewInfo& View, FRDGBuilder& GraphBuilder, FRadianceCacheParameters& OutParameters);

	bool IsEnabled(const FViewInfo& View);
	int32 GetClipmapGridResolution();
};