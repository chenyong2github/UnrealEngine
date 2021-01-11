// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "DecalRenderingCommon.h"

class FViewInfo;

struct FSceneTextures;

bool IsDBufferEnabled(const FSceneViewFamily& ViewFamily, EShaderPlatform ShaderPlatform);

struct FDBufferTextures
{
	bool IsValid() const
	{
		check(!DBufferA || (DBufferB && DBufferC));
		return HasBeenProduced(DBufferA);
	}

	FRDGTextureRef DBufferA = nullptr;
	FRDGTextureRef DBufferB = nullptr;
	FRDGTextureRef DBufferC = nullptr;
	FRDGTextureRef DBufferMask = nullptr;
};

FDBufferTextures CreateDBufferTextures(FRDGBuilder& GraphBuilder, FIntPoint Extent, EShaderPlatform ShaderPlatform);

BEGIN_SHADER_PARAMETER_STRUCT(FDBufferParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DBufferATexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DBufferBTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DBufferCTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, DBufferRenderMask)
	SHADER_PARAMETER_SAMPLER(SamplerState, DBufferATextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, DBufferBTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, DBufferCTextureSampler)
END_SHADER_PARAMETER_STRUCT()

FDBufferParameters GetDBufferParameters(FRDGBuilder& GraphBuilder, const FDBufferTextures& DBufferTextures, EShaderPlatform ShaderPlatform);

inline bool IsWritingToGBufferA(FDecalRenderingCommon::ERenderTargetMode RenderTargetMode)
{
	return RenderTargetMode == FDecalRenderingCommon::RTM_SceneColorAndGBufferWithNormal
		|| RenderTargetMode == FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteWithNormal
		|| RenderTargetMode == FDecalRenderingCommon::RTM_GBufferNormal;
}

inline bool IsWritingToDepth(FDecalRenderingCommon::ERenderTargetMode RenderTargetMode)
{
	return RenderTargetMode == FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteWithNormal
		|| RenderTargetMode == FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteNoNormal;
}

struct FDeferredDecalPassTextures
{
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer = nullptr;

	// Potential render targets for the decal pass.
	FRDGTextureMSAA Depth;
	FRDGTextureRef Color = nullptr;
	FRDGTextureRef ScreenSpaceAO = nullptr;
	FRDGTextureRef GBufferA = nullptr;
	FRDGTextureRef GBufferB = nullptr;
	FRDGTextureRef GBufferC = nullptr;
	FRDGTextureRef GBufferE = nullptr;
	FDBufferTextures* DBufferTextures = nullptr;
};

FDeferredDecalPassTextures GetDeferredDecalPassTextures(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures, FDBufferTextures* DBufferTextures);

void AddDeferredDecalPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	const FDeferredDecalPassTextures& Textures,
	EDecalRenderStage RenderStage);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDeferredDecalUniformParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, PreviousFrameNormal)
	SHADER_PARAMETER(int32, NormalReprojectionEnabled)
	SHADER_PARAMETER(float, NormalReprojectionThresholdLow)
	SHADER_PARAMETER(float, NormalReprojectionThresholdHigh)
	SHADER_PARAMETER(float, NormalReprojectionThresholdScaleHelper)
	SHADER_PARAMETER(FVector2D, NormalReprojectionJitter)
END_SHADER_PARAMETER_STRUCT()

TUniformBufferRef<FDeferredDecalUniformParameters> CreateDeferredDecalUniformBuffer(const FViewInfo& View);

BEGIN_SHADER_PARAMETER_STRUCT(FDeferredDecalPassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_STRUCT_REF(FDeferredDecalUniformParameters, DeferredDecal)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void GetDeferredDecalPassParameters(
	const FViewInfo& View,
	const FDeferredDecalPassTextures& DecalPassTextures,
	FDecalRenderingCommon::ERenderTargetMode RenderTargetMode,
	FDeferredDecalPassParameters& PassParameters);

void RenderMeshDecals(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FDeferredDecalPassTextures& DecalPassTextures,
	EDecalRenderStage DecalRenderStage);

void ExtractNormalsForNextFrameReprojection(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const TArray<FViewInfo>& Views);
