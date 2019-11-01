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

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDeepShadowPassUniformParameters, )
	SHADER_PARAMETER(FMatrix, WorldToClipMatrix)
	SHADER_PARAMETER(FVector4, SliceValue)
	SHADER_PARAMETER(FIntRect, AtlasRect)
	SHADER_PARAMETER(FVector, VoxelMinAABB)
	SHADER_PARAMETER(FVector, VoxelMaxAABB)
	SHADER_PARAMETER(uint32, VoxelResolution)
	SHADER_PARAMETER_TEXTURE(Texture2D<float>, FrontDepthTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// to be removed
void RasterHairStrands(
	FRHICommandList& RHICmdList,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsClusterData::TPrimitiveInfos& PrimitiveSceneInfos,
	const EHairStrandsRasterPassType ShadowPassType,
	const FIntRect& AtlasRect,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformShaderParameters,
	const TUniformBufferRef<FDeepShadowPassUniformParameters>& DeepShadowPassUniformParameters);