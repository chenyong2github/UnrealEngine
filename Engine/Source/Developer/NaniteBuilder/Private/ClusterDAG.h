// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cluster.h"

// Log CRCs to test for deterministic building
#if 0
	#define LOG_CRC( Array ) UE_LOG( LogStaticMesh, Log, TEXT(#Array " CRC %u"), FCrc::MemCrc32( Array.GetData(), Array.Num() * Array.GetTypeSize() ) )
#else
	#define LOG_CRC( Array )
#endif

namespace Nanite
{

struct FClusterGroup
{
	FSphere				Bounds;
	FSphere				LODBounds;
	float				MinLODError;
	float				MaxParentLODError;
	int32				MipLevel;
	
	uint32				PageIndexStart;
	uint32				PageIndexNum;
	TArray< uint32 >	Children;

	friend FArchive& operator<<(FArchive& Ar, FClusterGroup& Group);
};

class FClusterDAG
{
public:
	FClusterDAG( TArray< FCluster >& InCluster );
	
	void		Reduce();

	static const uint32 MinGroupSize = 8;
	static const uint32 MaxGroupSize = 32;
	
	FBounds		MeshBounds;
	
	TArray< FCluster >&		Clusters;
	TArray< FClusterGroup >	Groups;

	TArray< int32 >			MipEnds;

private:
	void		CompleteCluster( uint32 Index );
	void		Reduce( TArrayView< uint32 > Children, int32 GroupIndex );

	TAtomic< uint32 >	NumClusters;
	uint32				NumExternalEdges = 0;
};

} // namespace Nanite