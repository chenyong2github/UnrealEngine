// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneTextureParameters.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "SceneRenderTargets.h"
#include "SystemTextures.h"

FSceneTextureParameters GetSceneTextureParameters(FRDGBuilder& GraphBuilder)
{
	const FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	FSceneTextureParameters Parameters;

	// Should always have a depth buffer around allocated, since early z-pass is first.
	Parameters.SceneDepthTexture = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ, ERenderTargetTexture::ShaderResource);
	Parameters.SceneStencilTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(Parameters.SceneDepthTexture, PF_X24_G8));

	// Registers all the scene texture from the scene context. No fallback is provided to catch mistake at shader parameter validation time
	// when a pass is trying to access a resource before any other pass actually created it.
	Parameters.GBufferVelocityTexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.SceneVelocity);
	Parameters.GBufferATexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferA);
	Parameters.GBufferBTexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferB);
	Parameters.GBufferCTexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferC);
	Parameters.GBufferDTexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferD);
	Parameters.GBufferETexture = TryRegisterExternalTexture(GraphBuilder, SceneContext.GBufferE);
	Parameters.GBufferFTexture = RegisterExternalTextureWithFallback(GraphBuilder, SceneContext.GBufferF, GSystemTextures.BlackDummy);

	return Parameters;
}

FSceneTextureParameters GetSceneTextureParameters(FRDGBuilder& GraphBuilder, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextureUniformBuffer)
{
	FSceneTextureParameters Parameters;
	Parameters.SceneDepthTexture = (*SceneTextureUniformBuffer)->SceneDepthTexture;
	if (Parameters.SceneDepthTexture)
	{
		Parameters.SceneStencilTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(Parameters.SceneDepthTexture, PF_X24_G8));
	}
	Parameters.GBufferATexture = (*SceneTextureUniformBuffer)->GBufferATexture;
	Parameters.GBufferBTexture = (*SceneTextureUniformBuffer)->GBufferBTexture;
	Parameters.GBufferCTexture = (*SceneTextureUniformBuffer)->GBufferCTexture;
	Parameters.GBufferDTexture = (*SceneTextureUniformBuffer)->GBufferDTexture;
	Parameters.GBufferETexture = (*SceneTextureUniformBuffer)->GBufferETexture;
	Parameters.GBufferFTexture = (*SceneTextureUniformBuffer)->GBufferFTexture;
	Parameters.GBufferVelocityTexture = (*SceneTextureUniformBuffer)->GBufferVelocityTexture;
	return Parameters;
}

FSceneLightingChannelParameters GetSceneLightingChannelParameters(FRDGBuilder& GraphBuilder, FRDGTextureRef LightingChannelsTexture)
{
	FSceneLightingChannelParameters Parameters;

	// Lighting channels might be disabled when all lights are on the same channel.
	if (LightingChannelsTexture)
	{
		Parameters.SceneLightingChannels = LightingChannelsTexture;
		Parameters.bSceneLightingChannelsValid = true;
	}
	else
	{
		Parameters.SceneLightingChannels = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
		Parameters.bSceneLightingChannelsValid = false;
	}

	return Parameters;
}

FRDGTextureRef GetEyeAdaptationTexture(FRDGBuilder& GraphBuilder, const FSceneView& View)
{
	if (View.HasValidEyeAdaptationTexture())
	{
		return GraphBuilder.RegisterExternalTexture(View.GetEyeAdaptationTexture(), ERenderTargetTexture::Targetable, ERDGTextureFlags::MultiFrame);
	}
	else
	{
		return GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
	}
}

FRHIShaderResourceView* GetEyeAdaptationBuffer(const FSceneView& View)
{
	if (View.HasValidEyeAdaptationBuffer())
	{
		return View.GetEyeAdaptationBuffer()->SRV;
	}
	else
	{
		return GWhiteVertexBufferWithSRV->ShaderResourceViewRHI;
	}
}