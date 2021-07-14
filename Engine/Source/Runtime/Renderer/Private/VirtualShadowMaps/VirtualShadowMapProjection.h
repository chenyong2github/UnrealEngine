// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMapProjection.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "ScreenSpaceDenoise.h"

class FVirtualShadowMapClipmap;

enum class EVirtualShadowMapProjectionInputType
{
	GBuffer = 0,
	HairStrands = 1
};

void RenderVirtualShadowMapProjection(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	EVirtualShadowMapProjectionInputType InputType,
	const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap,
	FRDGTextureRef OutputShadowMaskTexture);

void RenderVirtualShadowMapProjection(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	EVirtualShadowMapProjectionInputType InputType,
	FProjectedShadowInfo* ShadowInfo,
	FRDGTextureRef OutputShadowMaskTexture);

FRDGTextureRef RenderVirtualShadowMapProjectionOnePass(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	EVirtualShadowMapProjectionInputType InputType);

void CompositeVirtualShadowMapMask(
	FRDGBuilder& GraphBuilder,
	const FIntRect ScissorRect,
	const FRDGTextureRef Input,
	bool bDirectionalLight,
	FRDGTextureRef OutputShadowMaskTexture);
