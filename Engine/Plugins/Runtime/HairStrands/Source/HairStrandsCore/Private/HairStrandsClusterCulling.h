// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsInterface.h"

struct FHairCullingParams
{
	bool bCullingProcessSkipped	= false;
};

struct FHairGroupInstance;
struct FHairStrandClusterData;
class FViewInfo;
class FGlobalShaderMap;
class FRDGBuilder;

void AddInstanceToClusterData(
	FHairGroupInstance* In,
	FHairStrandClusterData& Out);

void ComputeHairStrandsClustersCulling(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap& ShaderMap,
	const TArray<const FSceneView*>& Views,
	FHairStrandClusterData& ClusterDatas);
