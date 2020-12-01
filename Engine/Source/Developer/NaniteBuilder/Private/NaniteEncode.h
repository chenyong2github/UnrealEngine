// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define USE_CONSTRAINED_CLUSTERS		1	// must match define in NaniteDataDecode.ush
											// Enable to constrain clusters to no more than 256 vertices and no index references outside of trailing window of CONSTRAINED_CLUSTER_CACHE_SIZE vertices.

struct FBounds;
namespace Nanite
{
	struct FResources;
	struct FMaterialTriangle;
	struct FMaterialRange;
	class FCluster;
	struct FClusterGroup;
	
	void BuildMaterialRanges(const TArray<uint32>& TriangleIndices, const TArray<int32>& MaterialIndices, TArray<FMaterialTriangle, TInlineAllocator<128>>& MaterialTris, TArray<FMaterialRange, TInlineAllocator<4>>& MaterialRanges);
	void Encode(FResources& Resources, TArray<FCluster>& Clusters, TArray<FClusterGroup>& Groups, const FBounds& MeshBounds, uint32 NumMeshes, uint32 NumTexCoords, bool bHasColors);
} // namespace Nanite