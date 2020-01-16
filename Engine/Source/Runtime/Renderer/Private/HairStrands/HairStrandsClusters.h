// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsCulling.h: Hair strands culling implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsInterface.h"

struct FHairCullingParams
{
	bool bShadowViewMode		= false;		// Set to true for shadow views, all strand will be kept but those outside view will
	bool bCullingProcessSkipped	= false;
};

void ComputeHairStrandsClustersCulling(
	FRHICommandListImmediate& RHICmdList,
	TShaderMap<FGlobalShaderType>& ShaderMap,
	const TArray<FViewInfo>& Views,
	const FHairCullingParams& CullingParameters,
	FHairStrandClusterData& ClusterDatas);

void ResetHairStrandsClusterToLOD0(
	FRHICommandListImmediate& RHICmdList,
	TShaderMap<FGlobalShaderType>& ShaderMap,
	FHairStrandClusterData& ClusterDatas);