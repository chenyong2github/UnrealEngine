// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HairStrandsCluster.h: Hair strands macro group computation implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "HairStrandsVoxelization.h"
#include "HairStrandsDeepShadow.h"
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
	uint32 MacroGroupCount = 0;
	FRDGBufferRef MacroGroupAABBsBuffer = nullptr;
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
		FHairGroupPublicData* PublicDataPtr = nullptr;
		bool IsCullingEnable() const;
	};
	typedef TArray<PrimitiveInfo, SceneRenderingAllocator> TPrimitiveInfos;

	FVirtualVoxelNodeDesc VirtualVoxelNodeDesc;
	FHairStrandsDeepShadowDatas DeepShadowDatas;
	TPrimitiveInfos PrimitivesInfos;
	FBoxSphereBounds Bounds;
	FIntRect ScreenRect;
	uint32 MacroGroupId;

	bool bNeedScatterSceneLighting = false;
};

/// Store all hair strands macro group infos for a given view
struct FHairStrandsMacroGroupDatas
{
	TArray<FHairStrandsMacroGroupData, SceneRenderingAllocator> Datas;
	FDeepShadowResources DeepShadowResources;
	FVirtualVoxelResources VirtualVoxelResources;
	FHairMacroGroupAABBData MacroGroupResources;
};

/// Store all hair strands macro group info for all views
struct FHairStrandsMacroGroupViews
{
	TArray<FHairStrandsMacroGroupDatas, SceneRenderingAllocator> Views;
};

FHairStrandsMacroGroupViews CreateHairStrandsMacroGroups(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const TArray<FViewInfo>& Views);