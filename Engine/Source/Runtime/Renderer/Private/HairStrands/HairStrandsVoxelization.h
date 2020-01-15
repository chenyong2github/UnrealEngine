// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsVoxelization.h: Hair voxelization implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"

struct FHairStrandsVoxelResources
{
	TRefCountPtr<IPooledRenderTarget> DensityTexture;
	TRefCountPtr<IPooledRenderTarget> TangentXTexture;
	TRefCountPtr<IPooledRenderTarget> TangentYTexture;
	TRefCountPtr<IPooledRenderTarget> TangentZTexture;
	TRefCountPtr<IPooledRenderTarget> MaterialTexture;
	FMatrix WorldToClip;
	FVector MinAABB;
	FVector MaxAABB;
};

/// Global enable/disable for hair voxelization
bool IsHairStrandsVoxelizationEnable();
bool IsHairStrandsForVoxelTransmittanceAndShadowEnable();
float GetHairStrandsVoxelizationDensityScale();
float GetHairStrandsVoxelizationDepthBiasScale();

void VoxelizeHairStrands(
	FRHICommandListImmediate& RHICmdList,
	const class FScene* Scene,
	const TArray<FViewInfo>& Views,
	struct FHairStrandsClusterViews& DeepShadowClusterViews);