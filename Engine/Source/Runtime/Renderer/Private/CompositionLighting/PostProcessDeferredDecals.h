// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "DecalRenderingCommon.h"
#include "PostProcess/RenderingCompositionGraph.h"

class FViewInfo;

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

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDecalPassUniformParameters,)
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EyeAdaptationTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

struct FDeferredDecalPassTextures
{
	TRDGUniformBufferRef<FDecalPassUniformParameters> DecalPassUniformBuffer = nullptr;

	// Potential render targets for the decal pass.
	FRDGTextureMSAA Depth;
	FRDGTextureRef Color = nullptr;
	FRDGTextureRef ScreenSpaceAO = nullptr;
	FRDGTextureRef GBufferA = nullptr;
	FRDGTextureRef GBufferB = nullptr;
	FRDGTextureRef GBufferC = nullptr;
	FRDGTextureRef GBufferE = nullptr;

	// [Input / Output]: D-Buffer targets allocated on-demand for the D-Buffer pass.
	FRDGTextureRef DBufferA = nullptr;
	FRDGTextureRef DBufferB = nullptr;
	FRDGTextureRef DBufferC = nullptr;
	FRDGTextureRef DBufferMask = nullptr;

	ERenderTargetLoadAction DBufferLoadAction = ERenderTargetLoadAction::EClear;
};

FDeferredDecalPassTextures GetDeferredDecalPassTextures(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View);

void AddDeferredDecalPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	FDeferredDecalPassTextures& Textures,
	EDecalRenderStage RenderStage);

BEGIN_SHADER_PARAMETER_STRUCT(FDeferredDecalPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FDecalPassUniformParameters, DecalPass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void GetDeferredDecalPassParameters(
	const FViewInfo& View,
	FDeferredDecalPassTextures& DecalPassTextures,
	FDecalRenderingCommon::ERenderTargetMode RenderTargetMode,
	FDeferredDecalPassParameters& PassParameters);

void RenderMeshDecals(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FDeferredDecalPassTextures& DecalPassTextures,
	EDecalRenderStage DecalRenderStage);