// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	TRefCountPtr<IPooledRenderTarget> CategorizationTexture;
	TRefCountPtr<IPooledRenderTarget> ViewHairCountTexture;

	TRefCountPtr<IPooledRenderTarget> PPLLNodeCounterTexture;
	TRefCountPtr<IPooledRenderTarget> PPLLNodeIndexTexture;
	TRefCountPtr<FPooledRDGBuffer>	  PPLLNodeDataBuffer;

	TRefCountPtr<IPooledRenderTarget> NodeIndex;
	TRefCountPtr<FPooledRDGBuffer>	  NodeData;
	FShaderResourceViewRHIRef		  NodeDataSRV;
	TRefCountPtr<FPooledRDGBuffer>	  NodeCoord;
	FShaderResourceViewRHIRef		  NodeCoordSRV;
	TRefCountPtr<FPooledRDGBuffer>	  NodeIndirectArg;
	uint32							  NodeGroupSize = 0;
};

struct FHairStrandsVisibilityViews
{
	TArray<FHairStrandsVisibilityData, SceneRenderingAllocator> HairDatas;
};

FHairStrandsVisibilityViews RenderHairStrandsVisibilityBuffer(
	FRHICommandListImmediate& RHICmdList,
	const class FScene* Scene,
	const TArray<FViewInfo>& Views,
	TRefCountPtr<IPooledRenderTarget> GBufferBTexture,
	TRefCountPtr<IPooledRenderTarget> ColorTexture,
	TRefCountPtr<IPooledRenderTarget> DepthTexture,
	TRefCountPtr<IPooledRenderTarget> VelocityTexture,
	const struct FHairStrandsClusterViews& ClusterViews);

void SetUpViewHairRenderInfo(const FViewInfo& ViewInfo, bool bEnableMSAA, FVector4& OutHairRenderInfo);


uint32 GetPPLLMeanListElementCountPerPixel();
uint32 GetPPLLMaxTotalListElementCount(FIntPoint Resolution);

