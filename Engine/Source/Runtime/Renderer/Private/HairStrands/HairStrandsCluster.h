// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsCluster.h: Hair strands cluster computation implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsVoxelization.h"
#include "SceneManagement.h"

/// Hair cluster infos (i.e. group of mesh having hair material on them)
struct FHairStrandsClusterData
{
	struct PrimitiveInfo
	{
		FMeshBatchAndRelevance MeshBatchAndRelevance;
		uint32 MaterialId;
	};
	typedef TArray<PrimitiveInfo, SceneRenderingAllocator> TPrimitiveInfos;

	FVector GetMinBound() const { return VoxelResources.MinAABB; }
	FVector GetMaxBound() const { return VoxelResources.MaxAABB; }
	uint32  GetResolution() const { return VoxelResources.DensityTexture ? VoxelResources.DensityTexture->GetDesc().Extent.X : 0; }

	FHairStrandsVoxelResources VoxelResources;
	TPrimitiveInfos PrimitivesInfos;
	FBoxSphereBounds Bounds;
	FIntRect ScreenRect;
	uint32 ClusterId;
};

/// Store all hair strandscluster infos for a given view
struct FHairStrandsClusterDatas
{
	TArray<FHairStrandsClusterData, SceneRenderingAllocator> Datas;
};

/// Store all hair strands cluster info for all views
struct FHairStrandsClusterViews
{
	TArray<FHairStrandsClusterDatas, SceneRenderingAllocator> Views;
};

FHairStrandsClusterViews CreateHairStrandsClusters(
	FRHICommandListImmediate& RHICmdList,
	const FScene* Scene,
	const TArray<FViewInfo>& Views);