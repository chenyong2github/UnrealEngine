// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairRendering.h: Hair rendering implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsCluster.h"

enum class EHairStrandsRasterPassType : uint8
{
	FrontDepth,
	DeepOpacityMap,
	Voxelization,
	VoxelizationMaterial,
};

// to be removed
void RasterHairStrands(
	FRHICommandList& RHICmdList,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterData::TPrimitiveInfos& PrimitiveSceneInfos,
	const struct FHairStrandsLightDesc& LightDesc,
	const EHairStrandsRasterPassType ShadowPassType,
	FRHITexture* DeepShadowDepthTexture,
	const FIntRect& AtlasRect,
	const FVector MinAABB = FVector::ZeroVector,
	const FVector MaxAABB = FVector::ZeroVector);