// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairVisibilityRendering.h: Hair strands visibility buffer implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

class FViewInfo;
struct FVector;
struct FIntPoint;

struct FHairStrandsVisibilityData
{
	FRDGTextureRef DepthTexture = nullptr;
	FRDGTextureRef IDTexture = nullptr;
	FRDGTextureRef MaterialTexture = nullptr;
	FRDGTextureRef AttributeTexture = nullptr;
	FRDGTextureRef VelocityTexture = nullptr;
	FRDGTextureRef ResolveMaskTexture = nullptr;
	FRDGTextureRef CategorizationTexture = nullptr;
	FRDGTextureRef ViewHairCountTexture = nullptr;
	FRDGTextureRef ViewHairCountUintTexture = nullptr;
	FRDGTextureRef DepthTextureUint = nullptr;
	FRDGTextureRef EmissiveTexture = nullptr;

	FRDGTextureRef HairOnlyDepthTexture = nullptr;

	FRDGTextureRef ViewHairVisibilityTexture0 = nullptr;
	FRDGTextureRef ViewHairVisibilityTexture1 = nullptr;
	FRDGTextureRef ViewHairVisibilityTexture2 = nullptr;
	FRDGTextureRef ViewHairVisibilityTexture3 = nullptr;

	FRDGTextureRef LightChannelMaskTexture = nullptr;

	FRDGTextureRef	PPLLNodeCounterTexture = nullptr;
	FRDGTextureRef	PPLLNodeIndexTexture = nullptr;
	FRDGBufferRef	PPLLNodeDataBuffer = nullptr;
	uint32			MaxPPLLNodePerPixelCount = 0;
	uint32			MaxPPLLNodeCount = 0;

	FRDGTextureRef	TileIndexTexture = nullptr;
	FRDGBufferRef	TileBuffer = nullptr;
	FRDGBufferRef	TileIndirectArgs = nullptr;
	const uint32	TileSize = 8;
	const uint32	TileThreadGroupSize = 32;

	uint32			MaxSampleCount = 8;
	uint32			MaxNodeCount = 0;
	FRDGTextureRef	NodeCount = nullptr;
	FRDGTextureRef	NodeIndex = nullptr;
	FRDGBufferRef	NodeData = nullptr;
	FRDGBufferRef	NodeCoord = nullptr;
	FRDGBufferRef	NodeIndirectArg = nullptr;
	uint32			NodeGroupSize = 0;

	// Hair lighting is accumulated within this buffer
	// Allocated conservatively
	// User indirect dispatch for accumulating contribution
	FIntPoint SampleLightingViewportResolution;
	FRDGTextureRef SampleLightingBuffer = nullptr;
};

struct FHairStrandsVisibilityViews
{
	TArray<FHairStrandsVisibilityData, SceneRenderingAllocator> HairDatas;
};

FHairStrandsVisibilityViews RenderHairStrandsVisibilityBuffer(
	FRDGBuilder& GraphBuilder,
	const class FScene* Scene,
	const TArray<FViewInfo>& Views,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferATexture,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferBTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferCTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferDTexture,
	TRefCountPtr<IPooledRenderTarget> InSceneGBufferETexture,
	TRefCountPtr<IPooledRenderTarget> ColorTexture,
	TRefCountPtr<IPooledRenderTarget> DepthTexture,
	TRefCountPtr<IPooledRenderTarget> VelocityTexture,
	const struct FHairStrandsMacroGroupViews& MacroGroupViews);

void SetUpViewHairRenderInfo(const FViewInfo& ViewInfo, FVector4& OutHairRenderInfo, uint32& OutHairRenderInfoBits, uint32& OutHairComponents);

