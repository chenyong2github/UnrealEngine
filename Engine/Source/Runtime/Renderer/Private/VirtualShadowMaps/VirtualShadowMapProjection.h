// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMapProjection.h
=============================================================================*/

#pragma once

#include "VirtualShadowMapConfig.h"
#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "ScreenSpaceDenoise.h"

class FVirtualShadowMapClipmap;

enum class EVirtualShadowMapProjectionInputType
{
	GBuffer = 0,
	HairStrands = 1
};

void RenderVirtualShadowMapProjectionForDenoising(
	const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef SignalTexture,
	bool& bOutUseTemporalDenoising);

void RenderVirtualShadowMapProjectionForDenoising(
	FProjectedShadowInfo* ShadowInfo,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef SignalTexture,
	bool& bOutUseTemporalDenoising);

void RenderVirtualShadowMapProjection(
	const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef ScreenShadowMaskTexture,
	EVirtualShadowMapProjectionInputType InputType,
	bool bProjectingForForwardShading);

void RenderVirtualShadowMapProjection(
	FProjectedShadowInfo* ShadowInfo,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef ScreenShadowMaskTexture,
	EVirtualShadowMapProjectionInputType InputType,
	bool bProjectingForForwardShading);

void RenderVirtualShadowMapProjection(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	FRDGTextureRef ShadowMaskBits );

void CompositeVirtualShadowMapMask(
	FRDGBuilder& GraphBuilder,
	const FIntRect ScissorRect,
	const FSSDSignalTextures& InputSignal,
	FRDGTextureRef OutputShadowMaskTexture);
