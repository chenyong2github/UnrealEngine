// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricRenderTarget.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "RendererInterface.h"
#include "RenderResource.h"
#include "VolumetricRenderTargetViewStateData.h"

class FScene;
class FViewInfo;
struct FSceneWithoutWaterTextures;

void InitVolumetricRenderTargetForViews(FRDGBuilder& GraphBuilder, TArrayView<FViewInfo> Views);

void ReconstructVolumetricRenderTarget(
	FRDGBuilder& GraphBuilder,
	TArrayView<FViewInfo> Views,
	FRDGTextureRef SceneDepthTexture,
	FRDGTextureRef HalfResolutionDepthCheckerboardMinMaxTexture);

void ComposeVolumetricRenderTargetOverScene(
	FRDGBuilder& GraphBuilder,
	TArrayView<FViewInfo> Views,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthResolveTexture,
	bool bShouldRenderSingleLayerWater,
	const FSceneWithoutWaterTextures& WaterPassData);

void ComposeVolumetricRenderTargetOverSceneUnderWater(
	FRDGBuilder& GraphBuilder,
	TArrayView<FViewInfo> Views,
	const FSceneWithoutWaterTextures& WaterPassData);

bool ShouldViewRenderVolumetricCloudRenderTarget(const FViewInfo& ViewInfo);
bool IsVolumetricRenderTargetEnabled();