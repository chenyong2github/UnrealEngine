// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderGraph.h"
#include "DecalRenderingCommon.h"

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

	// [Input / Output]: D-Buffer targets allocated on-demand for the D-Buffer pass.
	FRDGTextureRef DBufferA = nullptr;
	FRDGTextureRef DBufferB = nullptr;
	FRDGTextureRef DBufferC = nullptr;
	FRDGTextureRef DBufferMask = nullptr;

	ERenderTargetLoadAction DBufferLoadAction = ERenderTargetLoadAction::EClear;
};

FDeferredDecalPassTextures GetDeferredDecalPassTextures(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer);

void AddDeferredDecalPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	FDeferredDecalPassTextures& Textures,
	EDecalRenderStage RenderStage);

BEGIN_SHADER_PARAMETER_STRUCT(FDeferredDecalPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDeferredDecalUniformParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, PreviousFrameNormal)
	SHADER_PARAMETER(int32, NormalReprojectionEnabled)
	SHADER_PARAMETER(float, NormalReprojectionThresholdLow)
	SHADER_PARAMETER(float, NormalReprojectionThresholdHigh)
	SHADER_PARAMETER(float, NormalReprojectionThresholdScaleHelper)
	SHADER_PARAMETER(FVector2D, NormalReprojectionJitter)
END_SHADER_PARAMETER_STRUCT()

void GetDeferredDecalPassParameters(
	const FViewInfo& View,
	FDeferredDecalPassTextures& DecalPassTextures,
	FDecalRenderingCommon::ERenderTargetMode RenderTargetMode,
	FDeferredDecalPassParameters& PassParameters);

void GetDeferredDecalUniformParameters(
	const FViewInfo& View,
	FDeferredDecalUniformParameters& UniformParameters);

void RenderMeshDecals(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FDeferredDecalPassTextures& DecalPassTextures,
	EDecalRenderStage DecalRenderStage);

void ExtractNormalsForNextFrameReprojection(
	FRDGBuilder& GraphBuilder,
	FSceneRenderTargets& SceneContext,
	const TArray<FViewInfo>& Views);
