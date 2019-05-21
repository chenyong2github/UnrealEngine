// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.h: Scene rendering definitions.
=============================================================================*/

#include "SceneViewFamilyBlackboard.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "SystemTextures.h"


void SetupSceneViewFamilyBlackboard(
	FRDGBuilder& GraphBuilder,
	FSceneViewFamilyBlackboard* OutBlackboard)
{
	const FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	*OutBlackboard = FSceneViewFamilyBlackboard();

	// Should always have a depth buffer around allocated, since early z-pass is first.
	OutBlackboard->SceneDepthBuffer = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ);

	// Registers all the scene texture from the scene context. No fallback is provided to catch mistake at shader parameter validation time
	// when a pass is trying to access a resource before any other pass actually created it.
	OutBlackboard->SceneVelocityBuffer = SceneContext.SceneVelocity ? GraphBuilder.RegisterExternalTexture(SceneContext.SceneVelocity) : nullptr;
	OutBlackboard->SceneGBufferA = SceneContext.GBufferA ? GraphBuilder.RegisterExternalTexture(SceneContext.GBufferA) : nullptr;
	OutBlackboard->SceneGBufferB = SceneContext.GBufferB ? GraphBuilder.RegisterExternalTexture(SceneContext.GBufferB) : nullptr;
	OutBlackboard->SceneGBufferC = SceneContext.GBufferC ? GraphBuilder.RegisterExternalTexture(SceneContext.GBufferC) : nullptr;
	OutBlackboard->SceneGBufferD = SceneContext.GBufferD ? GraphBuilder.RegisterExternalTexture(SceneContext.GBufferD) : nullptr;
	OutBlackboard->SceneGBufferE = SceneContext.GBufferE ? GraphBuilder.RegisterExternalTexture(SceneContext.GBufferE) : nullptr;

	// Ligthing channels might be disabled when all lights are on the same channel.
	if (SceneContext.LightingChannels)
	{
		OutBlackboard->SceneLightingChannels = GraphBuilder.RegisterExternalTexture(SceneContext.LightingChannels, TEXT("LightingChannels"));
		OutBlackboard->bIsSceneLightingChannelsValid = true;
	}
	else
	{
		OutBlackboard->SceneLightingChannels = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy, TEXT("LightingChannels"));
		OutBlackboard->bIsSceneLightingChannelsValid = false;
	}
}

void SetupSceneTextureSamplers(FSceneTextureSamplerParameters* OutSamplers)
{
	FRHISamplerState* Sampler = TStaticSamplerState<SF_Point>::GetRHI();
	OutSamplers->SceneDepthBufferSampler = Sampler;
	OutSamplers->SceneVelocityBufferSampler = Sampler;
	OutSamplers->SceneGBufferASampler = Sampler;
	OutSamplers->SceneGBufferBSampler = Sampler;
	OutSamplers->SceneGBufferCSampler = Sampler;
	OutSamplers->SceneGBufferDSampler = Sampler;
	OutSamplers->SceneGBufferESampler = Sampler;
}

FRDGTextureRef GetEyeAdaptationTexture(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	if (View.HasValidEyeAdaptation())
	{
		return GraphBuilder.RegisterExternalTexture(View.GetEyeAdaptation(GraphBuilder.RHICmdList), TEXT("ViewEyeAdaptation"));
	}
	else
	{
		return GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy, TEXT("DefaultViewEyeAdaptation"));
	}
}
