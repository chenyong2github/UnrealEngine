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
	TRefCountPtr<IPooledRenderTarget> DepthTexture;
	TRefCountPtr<IPooledRenderTarget> IDTexture;
	TRefCountPtr<IPooledRenderTarget> MaterialTexture;
	TRefCountPtr<IPooledRenderTarget> AttributeTexture;
	TRefCountPtr<IPooledRenderTarget> VelocityTexture;
	TRefCountPtr<IPooledRenderTarget> ResolveMaskTexture;
	TRefCountPtr<IPooledRenderTarget> CategorizationTexture;
	TRefCountPtr<IPooledRenderTarget> ViewHairCountTexture;
	TRefCountPtr<IPooledRenderTarget> ViewHairCountUintTexture;
	TRefCountPtr<IPooledRenderTarget> DepthTextureUint;

	TRefCountPtr<IPooledRenderTarget> ViewHairVisibilityTexture0;
	TRefCountPtr<IPooledRenderTarget> ViewHairVisibilityTexture1;
	TRefCountPtr<IPooledRenderTarget> ViewHairVisibilityTexture2;
	TRefCountPtr<IPooledRenderTarget> ViewHairVisibilityTexture3;

	TRefCountPtr<IPooledRenderTarget> LightChannelMaskTexture;

	TRefCountPtr<IPooledRenderTarget> PPLLNodeCounterTexture;
	TRefCountPtr<IPooledRenderTarget> PPLLNodeIndexTexture;
	TRefCountPtr<FRDGPooledBuffer>	  PPLLNodeDataBuffer;
	uint32							  MaxPPLLNodePerPixelCount = 0;
	uint32							  MaxPPLLNodeCount = 0;

	TRefCountPtr<IPooledRenderTarget> TileIndexTexture;
	TRefCountPtr<FRDGPooledBuffer>	  TileBuffer;
	TRefCountPtr<FRDGPooledBuffer>	  TileIndirectArgs;
	const uint32					  TileSize = 8;
	const uint32					  TileThreadGroupSize = 32;

	uint32							  MaxSampleCount = 8;
	uint32							  MaxNodeCount = 0;
	TRefCountPtr<IPooledRenderTarget> NodeCount;
	TRefCountPtr<IPooledRenderTarget> NodeIndex;
	TRefCountPtr<FRDGPooledBuffer>	  NodeData;
	FShaderResourceViewRHIRef		  NodeDataSRV;
	TRefCountPtr<FRDGPooledBuffer>	  NodeCoord;
	FShaderResourceViewRHIRef		  NodeCoordSRV;
	TRefCountPtr<FRDGPooledBuffer>	  NodeIndirectArg;
	uint32							  NodeGroupSize = 0;

	// Hair lighting is accumulated within this buffer
	// Allocated conservatively
	// User indirect dispatch for accumulating contribution
	FIntPoint SampleLightingViewportResolution;
	TRefCountPtr<IPooledRenderTarget> SampleLightingBuffer;
//	TRefCountPtr<IPooledRenderTarget> PixelLightingBuffer;

};

struct FHairStrandsVisibilityViews
{
	TArray<FHairStrandsVisibilityData, SceneRenderingAllocator> HairDatas;
};

FHairStrandsVisibilityViews RenderHairStrandsVisibilityBuffer(
	FRDGBuilder& GraphBuilder,
	const class FScene* Scene,
	const TArray<FViewInfo>& Views,
	TRefCountPtr<IPooledRenderTarget> GBufferBTexture,
	TRefCountPtr<IPooledRenderTarget> ColorTexture,
	TRefCountPtr<IPooledRenderTarget> DepthTexture,
	TRefCountPtr<IPooledRenderTarget> VelocityTexture,
	const struct FHairStrandsMacroGroupViews& MacroGroupViews);

void SetUpViewHairRenderInfo(const FViewInfo& ViewInfo, FVector4& OutHairRenderInfo, uint32& OutHairRenderInfoBits);

