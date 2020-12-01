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
	uint32				MeshIndex;
	
	uint32				PageIndexStart;
	uint32				PageIndexNum;
	TArray< uint32 >	Children;

	friend FArchive& operator<<(FArchive& Ar, FClusterGroup& Group);
};

// Performs DAG reduction and appends the resulting clusters and groups
void DAGReduce(TArray< FClusterGroup >& Groups, TArray< FCluster >& Cluster, uint32 ClusterBaseStart, uint32 ClusterBaseNum, uint32 MeshIndex, FBounds& MeshBounds, TArray<int32>* OutMipEnds);

} // namespace Nanite