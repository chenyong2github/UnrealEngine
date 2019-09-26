// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsDeepShadow.h: Hair strands deep shadow implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "SceneTypes.h"

/// Hold deep shadow information for a given light.
struct FHairStrandsDeepShadowData
{
	static const uint32 MaxClusterCount = 16u;

	TRefCountPtr<IPooledRenderTarget> DepthTexture;
	TRefCountPtr<IPooledRenderTarget> LayersTexture;
	FMatrix WorldToLightTransform;
	FIntRect AtlasRect;
	uint32 ClusterId = ~0;

	FIntPoint ShadowResolution = FIntPoint::ZeroValue;
	uint32 LightId = ~0;
	ELightComponentType LightType = LightType_MAX;
	FVector  LightDirection;
	FVector4 LightPosition;
	FLinearColor LightLuminance;

	FBoxSphereBounds Bounds;
};

/// Store all deep shadows infos for a given view
struct FHairStrandsDeepShadowDatas
{
	TArray<FHairStrandsDeepShadowData, SceneRenderingAllocator> Datas;
};

/// Store all deep shadows info for all views
struct FHairStrandsDeepShadowViews
{
	TArray<FHairStrandsDeepShadowDatas, SceneRenderingAllocator> Views;
};

FHairStrandsDeepShadowViews RenderHairStrandsDeepShadows(
	FRHICommandListImmediate& RHICmdList,
	const class FScene* Scene,
	const TArray<FViewInfo>& Views,
	const struct FHairStrandsClusterViews& ClusterViews);