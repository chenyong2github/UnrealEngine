// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "ScreenSpaceDenoise.h"

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

bool ShouldRenderScreenSpaceReflections(const FViewInfo& View);

bool ShouldRenderScreenSpaceDiffuseIndirect(const FViewInfo& View);

void GetSSRQualityForView(const FViewInfo& View, ESSRQuality* OutQuality, IScreenSpaceDenoiser::FReflectionsRayTracingConfig* OutRayTracingConfigs);

bool IsSSRTemporalPassRequired(const FViewInfo& View);

void RenderScreenSpaceReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View,
	ESSRQuality SSRQuality,
	bool bDenoiser,
	IScreenSpaceDenoiser::FReflectionsInputs* DenoiserInputs);

void RenderScreenSpaceDiffuseIndirect(
	FRDGBuilder& GraphBuilder, 
	const FSceneTextureParameters& SceneTextures,
	const FRDGTextureRef SceneColor,
	const FViewInfo& View,
	IScreenSpaceDenoiser::FDiffuseIndirectInputs* OutDenoiserInputs);
