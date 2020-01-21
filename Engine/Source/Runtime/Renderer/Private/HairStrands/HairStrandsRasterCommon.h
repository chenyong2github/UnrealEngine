// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairRendering.h: Hair rendering implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsCluster.h"
#include "HairStrandsVoxelization.h"

class FViewInfo;

enum class EHairStrandsRasterPassType : uint8
{
	FrontDepth,
	DeepOpacityMap,
	Voxelization,
	VoxelizationMaterial,
	VoxelizationVirtual
};

// ////////////////////////////////////////////////////////////////
// Deep shadow raster pass

BEGIN_SHADER_PARAMETER_STRUCT(FHairDeepShadowRasterPassParameters, )
	SHADER_PARAMETER(FMatrix, WorldToClipMatrix)
	SHADER_PARAMETER(FVector4, SliceValue)
	SHADER_PARAMETER(FIntRect, AtlasRect)
	SHADER_PARAMETER(FIntPoint, ViewportResolution)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FrontDepthTexture)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void AddHairDeepShadowRasterPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfos,
	const EHairStrandsRasterPassType ShadowPassType,
	const FIntRect& ViewportRect,
	const FVector4& HairRenderInfo,
	const FVector& LightDirection,
	FHairDeepShadowRasterPassParameters* PassParameters);

// ////////////////////////////////////////////////////////////////
// Voxelization raster pass
BEGIN_SHADER_PARAMETER_STRUCT(FHairVoxelizationRasterPassParameters, )
	SHADER_PARAMETER_STRUCT(FVirtualVoxelCommonParameters, VirtualVoxel)
	SHADER_PARAMETER(FMatrix, WorldToClipMatrix)
	SHADER_PARAMETER(FVector, VoxelMinAABB)
	SHADER_PARAMETER(FVector, VoxelMaxAABB)
	SHADER_PARAMETER(FIntVector, VoxelResolution)
	SHADER_PARAMETER(uint32, MacroGroupId)
	SHADER_PARAMETER(FIntPoint, ViewportResolution)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelizationViewInfo>, VoxelizationViewInfoBuffer)

	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, DensityTexture)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, TangentXTexture)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, TangentYTexture)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, TangentZTexture)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, MaterialTexture)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void AddHairVoxelizationRasterPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfos,
	const EHairStrandsRasterPassType ShadowPassType,
	const FIntRect& ViewportRect,
	const FVector4& HairRenderInfo,
	const FVector& RasterDirection,
	FHairVoxelizationRasterPassParameters* PassParameters);
