// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshletDAG.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "GraphPartitioner.h"
#include "MeshSimplify.h"

namespace Nanite
{

FClusterDAG::FClusterDAG( TArray< FCluster >& InClusters, TArray< FClusterGroup >& InClusterGroups )
	: Clusters( InClusters )
	, ClusterGroups( InClusterGroups )
	, NumClusters( Clusters.Num() )
{
	for( int i = 0; i < Clusters.Num(); i++ )
	{
		CompleteCluster(i);
	}
}

void FClusterDAG::Reduce( const FMeshNaniteSettings& Settings )
{
	int32 LevelOffset = 0;

	while( true )
	{
		MipEnds.Add( Clusters.Num() );

		TArrayView< FCluster > LevelClusters( &Clusters[ LevelOffset ], Clusters.Num() - LevelOffset );

		if( LevelClusters.Num() < 2 )
			break;
		
		struct FExternalEdge
		{
			uint32	ClusterIndex;
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
		ParallelFor( LevelClusters.Num(),
			[&]( uint32 ClusterIndex )
			{
				FCluster& Cluster = LevelClusters[ ClusterIndex ];

				for( TConstSetBitIterator<> SetBit( Cluster.ExternalEdges ); SetBit; ++SetBit )
				{
					uint32 EdgeIndex = SetBit.GetIndex();

					uint32 VertIndex0 = Cluster.Indexes[ EdgeIndex ];
					uint32 VertIndex1 = Cluster.Indexes[ Cycle3( EdgeIndex ) ];
	
					const FVector& Position0 = Cluster.GetPosition( VertIndex0 );
					const FVector& Position1 = Cluster.GetPosition( VertIndex1 );

					uint32 Hash0 = HashPosition( Position0 );
					uint32 Hash1 = HashPosition( Position1 );
					uint32 Hash = Murmur32( { Hash0, Hash1 } );

					uint32 ExternalEdgeIndex = ExternalEdgeOffset++;
					ExternalEdges[ ExternalEdgeIndex ] = { ClusterIndex, EdgeIndex };
					ExternalEdgeHash.Add_Concurrent( Hash, ExternalEdgeIndex );
				}
			} );

		check( ExternalEdgeOffset == ExternalEdges.Num() );

		TAtomic< uint32 > NumAdjacency(0);

		// Find matching edge in other clusters
		ParallelFor( LevelClusters.Num(),
			[&]( uint32 ClusterIndex )
			{
				FCluster& Cluster = LevelClusters[ ClusterIndex ];

				for( TConstSetBitIterator<> SetBit( Cluster.ExternalEdges ); SetBit; ++SetBit )
				{
					uint32 EdgeIndex = SetBit.GetIndex();

					uint32 VertIndex0 = Cluster.Indexes[ EdgeIndex ];
					uint32 VertIndex1 = Cluster.Indexes[ Cycle3( EdgeIndex ) ];
	
					const FVector& Position0 = Cluster.GetPosition( VertIndex0 );
					const FVector& Position1 = Cluster.GetPosition( VertIndex1 );

					uint32 Hash0 = HashPosition( Position0 );
					uint32 Hash1 = HashPosition( Position1 );
					uint32 Hash = Murmur32( { Hash1, Hash0 } );

					for( uint32 ExternalEdgeIndex = ExternalEdgeHash.First( Hash ); ExternalEdgeHash.IsValid( ExternalEdgeIndex ); ExternalEdgeIndex = ExternalEdgeHash.Next( ExternalEdgeIndex ) )
					{
						FExternalEdge ExternalEdge = ExternalEdges[ ExternalEdgeIndex ];

						FCluster& OtherCluster = LevelClusters[ ExternalEdge.ClusterIndex ];

						if( OtherCluster.ExternalEdges[ ExternalEdge.EdgeIndex ] )
						{
							uint32 OtherVertIndex0 = OtherCluster.Indexes[ ExternalEdge.EdgeIndex ];
							uint32 OtherVertIndex1 = OtherCluster.Indexes[ Cycle3( ExternalEdge.EdgeIndex ) ];
			
							if( Position0 == OtherCluster.GetPosition( OtherVertIndex1 ) &&
								Position1 == OtherCluster.GetPosition( OtherVertIndex0 ) )
							{
								// Found matching edge. Increase it's count.
								Cluster.AdjacentClusters.FindOrAdd( ExternalEdge.ClusterIndex, 0 )++;

								// Can't break or a triple edge might be non-deterministically connected.
								// Need to find all matching, not just first.
							}
						}
					}
				}
				NumAdjacency += Cluster.AdjacentClusters.Num();

				// Force deterministic order of adjacency.
				Cluster.AdjacentClusters.KeySort(
					[ &LevelClusters ]( uint32 A, uint32 B )
					{
						return LevelClusters[A].GUID < LevelClusters[B].GUID;
					} );
			} );

		FDisjointSet DisjointSet( LevelClusters.Num() );

		for( uint32 ClusterIndex = 0; ClusterIndex < (uint32)LevelClusters.Num(); ClusterIndex++ )
		{
			for( auto& Pair : LevelClusters[ ClusterIndex ].AdjacentClusters )
			{
				uint32 OtherClusterIndex = Pair.Key;

				uint32 Count = LevelClusters[ OtherClusterIndex ].AdjacentClusters.FindChecked( ClusterIndex );
				check( Count == Pair.Value );

				if( ClusterIndex > OtherClusterIndex )
				{
					DisjointSet.UnionSequential( ClusterIndex, OtherClusterIndex );
				}
			}
		}

		FGraphPartitioner Partitioner( LevelClusters.Num() );

		// Sort to force deterministic order
		{
			TArray< uint32 > SortedIndexes;
			SortedIndexes.AddUninitialized( Partitioner.Indexes.Num() );
			RadixSort32( SortedIndexes.GetData(), Partitioner.Indexes.GetData(), Partitioner.Indexes.Num(),
				[&]( uint32 Index )
				{
					return LevelClusters[ Index ].GUID;
				} );
			Swap( Partitioner.Indexes, SortedIndexes );
		}

		auto GetCenter = [&]( uint32 Index )
		{
			FBounds& Bounds = LevelClusters[ Index ].Bounds;
			return 0.5f * ( Bounds.Min + Bounds.Max );
		};
		Partitioner.BuildLocalityLinks( DisjointSet, MeshBounds, GetCenter );

		auto* RESTRICT Graph = Partitioner.NewGraph( NumAdjacency );

		for( int32 i = 0; i < LevelClusters.Num(); i++ )
		{
			Graph->AdjacencyOffset[i] = Graph->Adjacency.Num();

			uint32 ClusterIndex = Partitioner.Indexes[i];

			for( auto& Pair : LevelClusters[ ClusterIndex ].AdjacentClusters )
			{
				uint32 OtherClusterIndex = Pair.Key;
				uint32 NumSharedEdges = Pair.Value;

				const auto& Cluster0 = Clusters[ LevelOffset + ClusterIndex ];
				const auto& Cluster1 = Clusters[ LevelOffset + OtherClusterIndex ];

				bool bSiblings = Cluster0.GroupIndex != MAX_uint32 && Cluster0.GroupIndex == Cluster1.GroupIndex;

				Partitioner.AddAdjacency( Graph, OtherClusterIndex, NumSharedEdges * ( bSiblings ? 1 : 16 ) + 4 );
			}

			Partitioner.AddLocalityLinks( Graph, ClusterIndex, 1 );
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
				NumParentIndexes += Clusters[ Partitioner.Indexes[i] ].Indexes.Num();
			}
			MaxParents += FMath::DivideAndRoundUp( NumParentIndexes, FCluster::ClusterSize * 6 );
		}

		LevelOffset = Clusters.Num();

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
		Clusters.SetNum( NumClusters, false );

		for( int32 i = LevelOffset; i < Clusters.Num(); i++ )
		{
			CompleteCluster(i);
		}
	}
	
	// Max out root node
	int32 RootIndex = Clusters.Num() - 1;
	FClusterGroup RootClusterGroup;
	RootClusterGroup.Children.Add( RootIndex );
	RootClusterGroup.Bounds = Clusters[ RootIndex ].SphereBounds;
	RootClusterGroup.LODBounds = FSphere( 0 );
	RootClusterGroup.MaxLODError = 1e10f;
	RootClusterGroup.MinLODError = -1.0f;
	RootClusterGroup.MipLevel = MAX_int32;
	Clusters[ RootIndex ].GroupIndex = ClusterGroups.Num();
	ClusterGroups.Add( RootClusterGroup );
}

void FClusterDAG::Reduce( TArrayView< uint32 > Children, int32 ClusterGroupIndex )
{
	check( ClusterGroupIndex >= 0 );

	// Merge
	TArray< FCluster*, TInlineAllocator<16> > MergeList;
	for( int32 Child : Children )
	{
		MergeList.Add( &Clusters[ Child ] );
	}
	
	// Force a deterministic order
	MergeList.Sort(
		[]( const FCluster& A, const FCluster& B )
		{
			return A.GUID < B.GUID;
		} );

	FCluster Merged( MergeList );

	int32 NumParents = FMath::DivideAndRoundUp< int32 >( Merged.Indexes.Num(), FCluster::ClusterSize * 6 );
	int32 ParentStart = 0;
	int32 ParentEnd = 0;

	float ParentMinLODError = 0.0f;
	float ParentMaxLODError = 0.0f;

	for( int32 TargetClusterSize = FCluster::ClusterSize - 2; TargetClusterSize > FCluster::ClusterSize / 2; TargetClusterSize -= 2 )
	{
		int32 TargetNumTris = NumParents * TargetClusterSize;

		// Simplify
		ParentMinLODError = ParentMaxLODError = Merged.Simplify( TargetNumTris );

		// Split
		if( NumParents == 1 )
		{
			ParentEnd = ( NumClusters += NumParents );
			ParentStart = ParentEnd - NumParents;

			Clusters[ ParentStart ] = Merged;
			Clusters[ ParentStart ].Bound();
			break;
		}
		else
		{
			FGraphPartitioner Partitioner( Merged.Indexes.Num() / 3 );
			Merged.Split( Partitioner );

			if( Partitioner.Ranges.Num() <= NumParents )
			{
				NumParents = Partitioner.Ranges.Num();
				ParentEnd = ( NumClusters += NumParents );
				ParentStart = ParentEnd - NumParents;

				int32 Parent = ParentStart;
				for( auto& Range : Partitioner.Ranges )
				{
					Clusters[ Parent ] = FCluster( Merged, Range.Begin, Range.End, Partitioner.Indexes );
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
		Clusters[ Parent ].LODBounds			= ParentLODBound;
		Clusters[ Parent ].LODError				= ParentMaxLODError;
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
		Clusters[ Child ].GroupIndex = ClusterGroupIndex;
	}
}

void FClusterDAG::CompleteCluster( uint32 Index )
{
	FCluster& Cluster = Clusters[ Index ];
	
	NumVerts			+= Cluster.NumVerts;
	NumIndexes			+= Cluster.Indexes.Num();
	NumExternalEdges	+= Cluster.NumExternalEdges;
	MeshBounds			+= Cluster.Bounds;
}

} // namespace Nanite