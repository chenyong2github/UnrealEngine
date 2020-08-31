// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Rendering/NaniteResources.h"
#include "Cluster.h"

namespace Nanite
{

struct FClusterGroup
{
	FSphere				Bounds;
	FSphere				LODBounds;
	//FPackedBound		PackedBounds;
	float				MinLODError;
	float				MaxLODError;
	int32				MipLevel;
	
	uint32				PageIndexStart;
	uint32				PageIndexNum;
	TArray<uint32>		Children;
};

struct FHierarchyNode
{
	FSphere			Bounds[64];
	FSphere			LODBounds[64];
	//FPackedBound	PackedBounds[64];
	float			MinLODErrors[64];
	float			MaxLODErrors[64];
	uint32			ChildrenStartIndex[64];
	uint32			NumChildren[64];
	uint32			ClusterGroupPartIndex[64];
};

class FClusterDAG
{
public:
	FClusterDAG( TArray< FCluster >& InCluster, TArray< FClusterGroup >& InClusterGroups );
	
	void		Reduce( const FMeshNaniteSettings& Settings );
	
	uint32		NumVerts = 0;
	uint32		NumIndexes = 0;
	FBounds		MeshBounds;

	TArray< int32 >		MipEnds;

private:
	void		CompleteCluster( uint32 Index );
	void		Reduce( TArrayView< uint32 > Children, int32 ClusterGroupIndex );

	TArray< FCluster >&			Clusters;
	TArray< FClusterGroup >&	ClusterGroups;

	TAtomic< uint32 >	NumClusters;
	uint32				NumExternalEdges = 0;
};

} // namespace Nanite