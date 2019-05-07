// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ScreenSpaceReflections.h: Post processing Screen Space Reflections implementation.
=============================================================================*/

#pragma once

#include "RenderGraph.h"
#include "ScreenSpaceDenoise.h"

class FViewInfo;
class FSceneViewFamilyBlackboard;

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

void GetSSRQualityForView(const FViewInfo& View, ESSRQuality* OutQuality, IScreenSpaceDenoiser::FReflectionsRayTracingConfig* OutRayTracingConfigs);

bool IsSSRTemporalPassRequired(const FViewInfo& View);

void RenderScreenSpaceReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneViewFamilyBlackboard& SceneBlackboard,
	const FRDGTextureRef CurrentSceneColor,
	const FViewInfo& View,
	ESSRQuality SSRQuality,
	bool bDenoiser,
	IScreenSpaceDenoiser::FReflectionsInputs* DenoiserInputs);
