// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsCluster.h: Hair strands macro group computation implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsVoxelization.h"
#include "SceneManagement.h"

// -----------------------------
// Hair Data structures overview 
// -----------------------------
// A groom component contains one or several HairGroup. These hair group are send to the 
// render as mesh batches. These meshes batches are filtered/culled per view, and regroup 
// into HairMacroGroup for computing voxelization/DOM data, ...
//
// The hierarchy of the data structure is as follow:
//  * HairMacroGroup
//  * HairGroup
//  * HairCluster

struct FHairMacroGroupAABBData
{
	TRefCountPtr<FPooledRDGBuffer>	MacroGroupAABBsBuffer;
};

/// Hair macro group infos
struct FHairStrandsMacroGroupData
{
	// List of primitive/mesh batch within an instance group
	struct PrimitiveInfo
	{
		FMeshBatchAndRelevance MeshBatchAndRelevance;
		uint32 MaterialId;
		uint32 ResourceId;
		uint32 GroupIndex;
	};
	typedef TArray<PrimitiveInfo, SceneRenderingAllocator> TPrimitiveInfos;

	// List of unique group within an instance group
	struct PrimitiveGroup
	{
		uint32 ResourceId;
		uint32 GroupIndex;
	};
	typedef TArray<PrimitiveGroup, SceneRenderingAllocator> TPrimitiveGroups;


	FVector GetMinBound() const { return VoxelResources.MinAABB; }
	FVector GetMaxBound() const { return VoxelResources.MaxAABB; }
	uint32  GetResolution() const { return VoxelResources.DensityTexture ? VoxelResources.DensityTexture->GetDesc().Extent.X : 0; }

	FVirtualVoxelNodeDesc VirtualVoxelNodeDesc;
	FHairStrandsVoxelResources VoxelResources;
	TPrimitiveInfos PrimitivesInfos;
	TPrimitiveGroups PrimitivesGroups;
	FBoxSphereBounds Bounds;
	FIntRect ScreenRect;
	uint32 MacroGroupId;
};

/// Store all hair strands macro group infos for a given view
struct FHairStrandsMacroGroupDatas
{
	TArray<FHairStrandsMacroGroupData, SceneRenderingAllocator> Datas;
	FVirtualVoxelResources VirtualVoxelResources;
	FHairMacroGroupAABBData MacroGroupResources;
};

/// Store all hair strands macro group info for all views
struct FHairStrandsMacroGroupViews
{
	TArray<FHairStrandsMacroGroupDatas, SceneRenderingAllocator> Views;
};

FHairStrandsMacroGroupViews CreateHairStrandsMacroGroups(
	FRHICommandListImmediate& RHICmdList,
	const FScene* Scene,
	const TArray<FViewInfo>& Views);