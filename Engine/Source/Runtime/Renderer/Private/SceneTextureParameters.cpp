// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredShadingRenderer.h: Scene rendering definitions.
=============================================================================*/

#include "SceneTextureParameters.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "SystemTextures.h"


void SetupSceneTextureParameters(
	FRDGBuilder& GraphBuilder,
	FSceneTextureParameters* OutTextures)
{
	const FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	*OutTextures = FSceneTextureParameters();

	// Should always have a depth buffer around allocated, since early z-pass is first.
	OutTextures->SceneDepthBuffer = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ, TEXT("SceneDepthZ"));

	// Registers all the scene texture from the scene context. No fallback is provided to catch mistake at shader parameter validation time
	// when a pass is trying to access a resource before any other pass actually created it.
	OutTextures->SceneVelocityBuffer = GraphBuilder.TryRegisterExternalTexture(SceneContext.SceneVelocity, TEXT("VelocityBuffer"));
	OutTextures->SceneGBufferA = GraphBuilder.TryRegisterExternalTexture(SceneContext.GBufferA, TEXT("GBufferA"));
	OutTextures->SceneGBufferB = GraphBuilder.TryRegisterExternalTexture(SceneContext.GBufferB, TEXT("GBufferB"));
	OutTextures->SceneGBufferC = GraphBuilder.TryRegisterExternalTexture(SceneContext.GBufferC, TEXT("GBufferC"));
	OutTextures->SceneGBufferD = GraphBuilder.TryRegisterExternalTexture(SceneContext.GBufferD, TEXT("GBufferD"));
	OutTextures->SceneGBufferE = GraphBuilder.TryRegisterExternalTexture(SceneContext.GBufferE, TEXT("GBufferE"));

	// Lighting channels might be disabled when all lights are on the same channel.
	if (SceneContext.LightingChannels)
	{
		OutTextures->SceneLightingChannels = GraphBuilder.RegisterExternalTexture(SceneContext.LightingChannels, TEXT("LightingChannels"));
		OutTextures->bIsSceneLightingChannelsValid = true;
	}
	else
	{
		OutTextures->SceneLightingChannels = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy, TEXT("LightingChannels"));
		OutTextures->bIsSceneLightingChannelsValid = false;
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
