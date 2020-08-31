// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Rendering/NaniteResources.h"
#include "Meshlet.h"

namespace Nanite
{

class FMeshletDAG
{
public:
	FMeshletDAG( TArray< FMeshlet >& InMeshlets, TArray< FTriCluster >& InClusters, TArray< FClusterGroup >& InClusterGroups );
	
	void		Reduce( const FMeshNaniteSettings& Settings );
	
	uint32		NumVerts = 0;
	uint32		NumIndexes = 0;
	FBounds		MeshBounds;

	TArray< int32 >		MipEnds;

private:
	void		CompleteMeshlet( uint32 Index );
	void		Reduce( TArrayView< uint32 > Children, int32 ClusterGroupIndex );

	TArray< FMeshlet >&			Meshlets;
	TArray< FTriCluster >&		Clusters;
	TArray< FClusterGroup >&	ClusterGroups;

	TAtomic< uint32 >	NumMeshlets;
	uint32				NumExternalEdges = 0;
};

} // namespace Nanite