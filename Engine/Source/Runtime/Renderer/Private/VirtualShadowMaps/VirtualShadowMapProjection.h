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

FRDGTextureRef RenderVirtualShadowMapProjection(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef HairCategorization,
	const TSharedPtr<FVirtualShadowMapClipmap>& Clipmap);

FRDGTextureRef RenderVirtualShadowMapProjection(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray,
	const FIntRect ScissorRect,
	FRDGTextureRef HairCategorization,
	FProjectedShadowInfo* ShadowInfo);

FRDGTextureRef RenderVirtualShadowMapProjectionOnePass(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FViewInfo& View,
	FVirtualShadowMapArray& VirtualShadowMapArray );

void CompositeVirtualShadowMapMask(
	FRDGBuilder& GraphBuilder,
	const FIntRect ScissorRect,
	const FRDGTextureRef Input,
	FRDGTextureRef OutputShadowMaskTexture);
