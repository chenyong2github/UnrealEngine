// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsCulling.h: Hair strands culling implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsInterface.h"
#include "Renderer/Private/SceneRendering.h"

struct FHairCullingParams
{
	bool bShadowViewMode		= false;		// Set to true for shadow views, all strand will be kept but those outside view will
	bool bCullingProcessSkipped	= false;
};

void ComputeHairStrandsClustersCulling(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap& ShaderMap,
	const TArray<FViewInfo>& Views,
	const FHairCullingParams& CullingParameters,
	FHairStrandClusterData& ClusterDatas);
