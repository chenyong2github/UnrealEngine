// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairRendering.h: Hair rendering implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "HairStrandsUtils.h"
#include "HairStrandsCluster.h"
#include "HairStrandsClusters.h"
#include "HairStrandsLUT.h"
#include "HairStrandsDeepShadow.h"
#include "HairStrandsVoxelization.h"
#include "HairStrandsVisibility.h"
#include "HairStrandsTransmittance.h"
#include "HairStrandsEnvironment.h"
#include "HairStrandsComposition.h"
#include "HairStrandsDebug.h"
#include "HairStrandsInterface.h"

/// Hold all the hair strands data
struct FHairStrandsDatas
{
	FHairStrandsVisibilityViews HairVisibilityViews;
	FHairStrandsMacroGroupViews MacroGroupsPerViews;
	FHairStrandsDebugData DebugData;
};

enum class EHairStrandsInterpolationType
{
	RenderStrands,
	SimulationStrands
};

void RunHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList, 
	EWorldType::Type WorldType, 
	const class FGPUSkinCache* GPUSkinCache,
	const struct FShaderDrawDebugData* DebugShaderData,
	FGlobalShaderMap* ShaderMap, 
	EHairStrandsInterpolationType Type,
	FHairStrandClusterData* ClusterData);

void RenderHairBasePass(
	FRHICommandListImmediate& RHICmdList,
	FScene* Scene,
	FSceneRenderTargets& SceneContext,
	TArray<FViewInfo>& Views,
	FHairStrandClusterData HairClusterData,
	FHairStrandsDatas& OutHairDatas);