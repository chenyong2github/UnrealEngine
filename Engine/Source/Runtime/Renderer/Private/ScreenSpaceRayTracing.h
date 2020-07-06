// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "ScreenSpaceDenoise.h"
#include "IndirectLightRendering.h"
#include "Lumen/LumenProbeHierarchy.h"

class FViewInfo;
class FSceneTextureParameters;


enum class ESSRQuality
{
	VisualizeSSR,

	Low,
	Medium,
	High,
	Epic,

	MAX
};

struct FTiledScreenSpaceReflection
{
	FRDGBufferRef TileListDataBuffer;
	FRDGBufferRef DispatchIndirectParametersBuffer;
	FRDGBufferUAVRef DispatchIndirectParametersBufferUAV;
	FRDGBufferUAVRef TileListStructureBufferUAV;
	FRDGBufferSRVRef TileListStructureBufferSRV;
	uint32 TileSize;
};

namespace ScreenSpaceRayTracing
{

BEGIN_SHADER_PARAMETER_STRUCT(FPrevSceneColorMip, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneAlpha)
END_SHADER_PARAMETER_STRUCT()

bool ShouldKeepBleedFreeSceneColor(const FViewInfo& View);

bool ShouldRenderScreenSpaceReflections(const FViewInfo& View);


void ProcessForNextFrameScreenSpaceRayTracing(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View);

void GetSSRQualityForView(const FViewInfo& View, ESSRQuality* OutQuality, IScreenSpaceDenoiser::FReflectionsRayTracingConfig* OutRayTracingConfigs);

bool IsSSRTemporalPassRequired(const FViewInfo& View);

int32 GetSSGIRayCountPerTracingPixel();


FPrevSceneColorMip ReducePrevSceneColorMip(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View);


void RenderScreenSpaceReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View,
	ESSRQuality SSRQuality,
	bool bDenoiser,
	IScreenSpaceDenoiser::FReflectionsInputs* DenoiserInputs,
	FTiledScreenSpaceReflection* TiledScreenSpaceReflection = nullptr);

bool IsScreenSpaceDiffuseIndirectSupported(const FViewInfo& View);

IScreenSpaceDenoiser::FDiffuseIndirectInputs CastStandaloneDiffuseIndirectRays(
	FRDGBuilder& GraphBuilder, 
	const HybridIndirectLighting::FCommonParameters& CommonParameters,
	const FPrevSceneColorMip& PrevSceneColor,
	const FViewInfo& View);

void CastDiffuseIndirectHybridRays(
	FRDGBuilder& GraphBuilder,
	const HybridIndirectLighting::FCommonParameters& CommonParameters,
	const FPrevSceneColorMip& PrevSceneColor,
	const FViewInfo& View);

void TraceProbe(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FSceneTextureParameters& SceneTextures,
	const FPrevSceneColorMip& PrevSceneColor,
	const LumenProbeHierarchy::FHierarchyParameters& HierarchyParameters,
	const LumenProbeHierarchy::FIndirectLightingAtlasParameters& IndirectLightingAtlasParameters);

void TraceIndirectProbeOcclusion(
	FRDGBuilder& GraphBuilder,
	const HybridIndirectLighting::FCommonParameters& CommonParameters,
	const FPrevSceneColorMip& PrevSceneColor,
	const FViewInfo& View,
	const LumenProbeHierarchy::FIndirectLightingProbeOcclusionParameters& ProbeOcclusionParameters);

} // namespace ScreenSpaceRayTracing
