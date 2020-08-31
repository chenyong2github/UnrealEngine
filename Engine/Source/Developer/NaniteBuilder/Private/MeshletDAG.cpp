// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshletDAG.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "GraphPartitioner.h"
#include "MeshSimplify.h"

namespace Nanite
{

FMeshletDAG::FMeshletDAG( TArray< FMeshlet >& InMeshlets, TArray< FTriCluster >& InClusters, TArray< FClusterGroup >& InClusterGroups )
	: Meshlets( InMeshlets )
	, Clusters( InClusters )
	, ClusterGroups( InClusterGroups )
	, NumMeshlets( Meshlets.Num() )
{
	for( int i = 0; i < Meshlets.Num(); i++ )
	{
		CompleteMeshlet(i);
	}
}

void FMeshletDAG::Reduce( const FMeshNaniteSettings& Settings )
{
	int32 LevelOffset = 0;

	while( true )
	{
		MipEnds.Add( Meshlets.Num() );

		TArrayView< FMeshlet > LevelMeshlets( &Meshlets[ LevelOffset ], Meshlets.Num() - LevelOffset );

		if( LevelMeshlets.Num() < 2 )
			break;
		
		struct FExternalEdge
		{
			uint32	MeshletIndex;
			uint32	EdgeIndex;
		};
		TArray< FExternalEdge >	ExternalEdges;
		FHashTable				ExternalEdgeHash;
		TAtomic< uint32 >		ExternalEdgeOffset(0);

		// We have a total count of NumExternalEdges so we can allocate a hash table without growing.
		ExternalEdges.AddUninitialized( NumExternalEdges );
		ExternalEdgeHash.Clear( 1 << FMath::FloorLog2( NumExternalEdges ), NumExternalEdges );
		NumExternalEdges = 0;

		// Add edges to hash table
		ParallelFor( LevelMeshlets.Num(),
			[&]( uint32 MeshletIndex )
			{
				FMeshlet& Meshlet = LevelMeshlets[ MeshletIndex ];

				for( TConstSetBitIterator<> SetBit( Meshlet.ExternalEdges ); SetBit; ++SetBit )
				{
					uint32 EdgeIndex = SetBit.GetIndex();

					uint32 VertIndex0 = Meshlet.Indexes[ EdgeIndex ];
					uint32 VertIndex1 = Meshlet.Indexes[ Cycle3( EdgeIndex ) ];
	
					const FVector& Position0 = Meshlet.GetPosition( VertIndex0 );
					const FVector& Position1 = Meshlet.GetPosition( VertIndex1 );

					uint32 Hash0 = HashPosition( Position0 );
					uint32 Hash1 = HashPosition( Position1 );
					uint32 Hash = Murmur32( { Hash0, Hash1 } );

					uint32 ExternalEdgeIndex = ExternalEdgeOffset++;
					ExternalEdges[ ExternalEdgeIndex ] = { MeshletIndex, EdgeIndex };
					ExternalEdgeHash.Add_Concurrent( Hash, ExternalEdgeIndex );
				}
			} );

		check( ExternalEdgeOffset == ExternalEdges.Num() );

		TAtomic< uint32 > NumAdjacency(0);

		// Find matching edge in other meshlets
		ParallelFor( LevelMeshlets.Num(),
			[&]( uint32 MeshletIndex )
			{
				FMeshlet& Meshlet = LevelMeshlets[ MeshletIndex ];

				for( TConstSetBitIterator<> SetBit( Meshlet.ExternalEdges ); SetBit; ++SetBit )
				{
					uint32 EdgeIndex = SetBit.GetIndex();

					uint32 VertIndex0 = Meshlet.Indexes[ EdgeIndex ];
					uint32 VertIndex1 = Meshlet.Indexes[ Cycle3( EdgeIndex ) ];
	
					const FVector& Position0 = Meshlet.GetPosition( VertIndex0 );
					const FVector& Position1 = Meshlet.GetPosition( VertIndex1 );

					uint32 Hash0 = HashPosition( Position0 );
					uint32 Hash1 = HashPosition( Position1 );
					uint32 Hash = Murmur32( { Hash1, Hash0 } );

					for( uint32 ExternalEdgeIndex = ExternalEdgeHash.First( Hash ); ExternalEdgeHash.IsValid( ExternalEdgeIndex ); ExternalEdgeIndex = ExternalEdgeHash.Next( ExternalEdgeIndex ) )
					{
						FExternalEdge ExternalEdge = ExternalEdges[ ExternalEdgeIndex ];

						FMeshlet& OtherMeshlet = LevelMeshlets[ ExternalEdge.MeshletIndex ];

						if( OtherMeshlet.ExternalEdges[ ExternalEdge.EdgeIndex ] )
						{
							uint32 OtherVertIndex0 = OtherMeshlet.Indexes[ ExternalEdge.EdgeIndex ];
							uint32 OtherVertIndex1 = OtherMeshlet.Indexes[ Cycle3( ExternalEdge.EdgeIndex ) ];
			
							if( Position0 == OtherMeshlet.GetPosition( OtherVertIndex1 ) &&
								Position1 == OtherMeshlet.GetPosition( OtherVertIndex0 ) )
							{
								// Found matching edge. Increase it's count.
								Meshlet.AdjacentMeshlets.FindOrAdd( ExternalEdge.MeshletIndex, 0 )++;

								// Can't break or a triple edge might be non-deterministically connected.
								// Need to find all matching, not just first.
							}
						}
					}
				}
				NumAdjacency += Meshlet.AdjacentMeshlets.Num();

				// Force deterministic order of adjacency.
				Meshlet.AdjacentMeshlets.KeySort(
					[ &LevelMeshlets ]( uint32 A, uint32 B )
					{
						return LevelMeshlets[A].GUID < LevelMeshlets[B].GUID;
					} );
			} );

		FDisjointSet DisjointSet( LevelMeshlets.Num() );

		for( uint32 MeshletIndex = 0; MeshletIndex < (uint32)LevelMeshlets.Num(); MeshletIndex++ )
		{
			for( auto& Pair : LevelMeshlets[ MeshletIndex ].AdjacentMeshlets )
			{
				uint32 OtherMeshletIndex = Pair.Key;

				uint32 Count = LevelMeshlets[ OtherMeshletIndex ].AdjacentMeshlets.FindChecked( MeshletIndex );
				check( Count == Pair.Value );

				if( MeshletIndex > OtherMeshletIndex )
				{
					DisjointSet.UnionSequential( MeshletIndex, OtherMeshletIndex );
				}
			}
		}

		FGraphPartitioner Partitioner( LevelMeshlets.Num() );

		// Sort to force deterministic order
		{
			TArray< uint32 > SortedIndexes;
			SortedIndexes.AddUninitialized( Partitioner.Indexes.Num() );
			RadixSort32( SortedIndexes.GetData(), Partitioner.Indexes.GetData(), Partitioner.Indexes.Num(),
				[&]( uint32 Index )
				{
					return LevelMeshlets[ Index ].GUID;
				} );
			Swap( Partitioner.Indexes, SortedIndexes );
		}

		auto GetCenter = [&]( uint32 Index )
		{
			FBounds& Bounds = LevelMeshlets[ Index ].Bounds;
			return 0.5f * ( Bounds.Min + Bounds.Max );
		};
		Partitioner.BuildLocalityLinks( DisjointSet, MeshBounds, GetCenter );

		auto* RESTRICT Graph = Partitioner.NewGraph( NumAdjacency );

		for( int32 i = 0; i < LevelMeshlets.Num(); i++ )
		{
			Graph->AdjacencyOffset[i] = Graph->Adjacency.Num();

			uint32 MeshletIndex = Partitioner.Indexes[i];

			for( auto& Pair : LevelMeshlets[ MeshletIndex ].AdjacentMeshlets )
			{
				uint32 OtherMeshletIndex = Pair.Key;
				uint32 NumSharedEdges = Pair.Value;

				const auto& Cluster0 = Clusters[ LevelOffset + MeshletIndex ];
				const auto& Cluster1 = Clusters[ LevelOffset + OtherMeshletIndex ];

				bool bSiblings = Cluster0.ClusterGroupIndex != MAX_uint32 && Cluster0.ClusterGroupIndex == Cluster1.ClusterGroupIndex;

				Partitioner.AddAdjacency( Graph, OtherMeshletIndex, NumSharedEdges * ( bSiblings ? 1 : 16 ) + 4 );
			}

			Partitioner.AddLocalityLinks( Graph, MeshletIndex, 1 );
		}
		Graph->AdjacencyOffset[ Graph->Num ] = Graph->Adjacency.Num();

		//UE_LOG( LogStaticMesh, Log, TEXT("Adjacency CRC %u"), FCrc::MemCrc32( Graph->Adjacency.GetData(), Graph->Adjacency.Num() * Graph->Adjacency.GetTypeSize() ) );
		//UE_LOG( LogStaticMesh, Log, TEXT("AdjacencyCost CRC %u"), FCrc::MemCrc32( Graph->AdjacencyCost.GetData(), Graph->AdjacencyCost.Num() * Graph->AdjacencyCost.GetTypeSize() ) );
		//UE_LOG( LogStaticMesh, Log, TEXT("AdjacencyOffset CRC %u"), FCrc::MemCrc32( Graph->AdjacencyOffset.GetData(), Graph->AdjacencyOffset.Num() * Graph->AdjacencyOffset.GetTypeSize() ) );

		Partitioner.PartitionStrict( Graph, 8, 32, true );
		//Partitioner.Partition( Graph, 8, 32 );

		//UE_LOG( LogStaticMesh, Log, TEXT("Partitioner.Ranges CRC %u"), FCrc::MemCrc32( Partitioner.Ranges.GetData(), Partitioner.Ranges.Num() * Partitioner.Ranges.GetTypeSize() ) );

		uint32 MaxParents = 0;
		for( auto& Range : Partitioner.Ranges )
		{
			uint32 NumParentIndexes = 0;
			for( uint32 i = Range.Begin; i < Range.End; i++ )
			{
				// Global indexing is needed in Reduce()
				Partitioner.Indexes[i] += LevelOffset;
				NumParentIndexes += Meshlets[ Partitioner.Indexes[i] ].Indexes.Num();
			}
			MaxParents += FMath::DivideAndRoundUp( NumParentIndexes, FMeshlet::ClusterSize * 6 );
		}

		LevelOffset = Meshlets.Num();

		Meshlets.AddDefaulted( MaxParents );
		Clusters.AddDefaulted( MaxParents );
		ClusterGroups.AddDefaulted( Partitioner.Ranges.Num() );

		ParallelFor( Partitioner.Ranges.Num(),
			[&]( int32 PartitionIndex )
			{
				auto& Range = Partitioner.Ranges[ PartitionIndex ];

				TArrayView< uint32 > Children( &Partitioner.Indexes[ Range.Begin ], Range.End - Range.Begin );
				uint32 ClusterGroupIndex = PartitionIndex + ClusterGroups.Num() - Partitioner.Ranges.Num();

				Reduce( Children, ClusterGroupIndex );
			} );

		// Correct num to atomic count
		Meshlets.SetNum( NumMeshlets, false );
		Clusters.SetNum( NumMeshlets, false );

		for( int32 i = LevelOffset; i < Meshlets.Num(); i++ )
		{
			CompleteMeshlet(i);
		}
	}
	
	// Max out root node
	int32 RootIndex = Meshlets.Num() - 1;
	FClusterGroup RootClusterGroup;
	RootClusterGroup.Children.Add( RootIndex );
	RootClusterGroup.Bounds = Clusters[ RootIndex ].SphereBounds;
	RootClusterGroup.LODBounds = FSphere( 0 );
	RootClusterGroup.MaxLODError = 1e10f;
	RootClusterGroup.MinLODError = -1.0f;
	RootClusterGroup.MipLevel = MAX_int32;
	Clusters[ RootIndex ].ClusterGroupIndex = ClusterGroups.Num();
	ClusterGroups.Add( RootClusterGroup );
}

void FMeshletDAG::Reduce( TArrayView< uint32 > Children, int32 ClusterGroupIndex )
{
	check( ClusterGroupIndex >= 0 );

	// Merge
	TArray< FMeshlet*, TInlineAllocator<16> > MergeList;
	for( int32 Child : Children )
	{
		MergeList.Add( &Meshlets[ Child ] );
	}
	
	// Force a deterministic order
	MergeList.Sort(
		[]( const FMeshlet& A, const FMeshlet& B )
		{
			return A.GUID < B.GUID;
		} );

	FMeshlet Merged( MergeList );

	int32 NumParents = FMath::DivideAndRoundUp< int32 >( Merged.Indexes.Num(), FMeshlet::ClusterSize * 6 );
	int32 ParentStart = 0;
	int32 ParentEnd = 0;

	float ParentMinLODError = 0.0f;
	float ParentMaxLODError = 0.0f;

	for( int32 TargetClusterSize = FMeshlet::ClusterSize - 2; TargetClusterSize > FMeshlet::ClusterSize / 2; TargetClusterSize -= 2 )
	{
		int32 TargetNumTris = NumParents * TargetClusterSize;

		// Simplify
		ParentMinLODError = ParentMaxLODError = Merged.Simplify( TargetNumTris );

		// Split
		if( NumParents == 1 )
		{
			ParentEnd = ( NumMeshlets += NumParents );
			ParentStart = ParentEnd - NumParents;

			Meshlets[ ParentStart ] = Merged;
			Clusters[ ParentStart ] = BuildCluster( Merged );
			break;
		}
		else
		{
			FGraphPartitioner Partitioner( Merged.Indexes.Num() / 3 );
			Merged.Split( Partitioner );

			if( Partitioner.Ranges.Num() <= NumParents )
			{
				NumParents = Partitioner.Ranges.Num();
				ParentEnd = ( NumMeshlets += NumParents );
				ParentStart = ParentEnd - NumParents;

				int32 Parent = ParentStart;
				for( auto& Range : Partitioner.Ranges )
				{
					Meshlets[ Parent ] = FMeshlet( Merged, Range.Begin, Range.End, Partitioner.Indexes );
					Clusters[ Parent ] = BuildCluster( Meshlets[ Parent ] );
					Parent++;
				}

				break;
			}
		}
	}

	TArray< FSphere, TInlineAllocator<32> > LODBoundSpheres;
	
	// Force parents to have same LOD data. They are all dependent.
	for( int32 Parent = ParentStart; Parent < ParentEnd; Parent++ )
	{
		LODBoundSpheres.Add( Clusters[ Parent ].LODBounds );
	}

	TArray< FSphere, TInlineAllocator<32> > ChildSpheres;
					
	// Force monotonic nesting.
	for( int32 Child : Children )
	{
		bool bLeaf = Clusters[ Child ].EdgeLength < 0.0f;
		float LODError = Clusters[ Child ].LODError;

		LODBoundSpheres.Add( Clusters[ Child ].LODBounds );
		ChildSpheres.Add( Clusters[ Child ].SphereBounds );
		ParentMinLODError = FMath::Min( ParentMinLODError, bLeaf ? -1.0f : LODError );
		ParentMaxLODError = FMath::Max( ParentMaxLODError, LODError );
	}

	FSphere	ParentLODBound( LODBoundSpheres.GetData(), LODBoundSpheres.Num() );
	FSphere	ParentBound( ChildSpheres.GetData(), ChildSpheres.Num() );

	for( int32 Parent = ParentStart; Parent < ParentEnd; Parent++ )
	{
		Clusters[ Parent ].LODBounds		= ParentLODBound;
		Clusters[ Parent ].LODError			= ParentMaxLODError;
		Clusters[ Parent ].GeneratingGroupIndex = ClusterGroupIndex;
	}

	ClusterGroups[ ClusterGroupIndex ].Bounds		= ParentBound;
	ClusterGroups[ ClusterGroupIndex ].LODBounds	= ParentLODBound;
	ClusterGroups[ ClusterGroupIndex ].MinLODError	= ParentMinLODError;
	ClusterGroups[ ClusterGroupIndex ].MaxLODError	= ParentMaxLODError;
	ClusterGroups[ ClusterGroupIndex ].MipLevel		= Merged.MipLevel;

	// Parents are completed, match parent data.
	for( int32 Child : Children )
	{
		check( ClusterGroups[ ClusterGroupIndex ].Children.Num() <= MAX_CLUSTERS_PER_GROUP_TARGET);
		ClusterGroups[ ClusterGroupIndex ].Children.Add( Child );
		Clusters[ Child ].ClusterGroupIndex = ClusterGroupIndex;
	}
}

void FMeshletDAG::CompleteMeshlet( uint32 Index )
{
	FMeshlet&    Meshlet = Meshlets[ Index ];
	FTriCluster& Cluster = Clusters[ Index ];
	
	NumVerts			+= Meshlet.NumVerts;
	NumIndexes			+= Meshlet.Indexes.Num();
	NumExternalEdges	+= Meshlet.NumExternalEdges;
	MeshBounds			+= Meshlet.Bounds;
}

} // namespace Nanite