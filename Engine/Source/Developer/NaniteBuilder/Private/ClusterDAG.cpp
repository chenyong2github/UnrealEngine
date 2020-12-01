// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClusterDAG.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "GraphPartitioner.h"
#include "MeshSimplify.h"

namespace Nanite
{

static const uint32 MinGroupSize = 8;
static const uint32 MaxGroupSize = 32;

static void DAGReduce( TArray< FClusterGroup >& Groups, TArray< FCluster >& Clusters, TAtomic< uint32 >& NumClusters, TArrayView< uint32 > Children, int32 GroupIndex, uint32 MeshIndex );

void DAGReduce(TArray< FClusterGroup >& Groups, TArray< FCluster >& Clusters, uint32 ClusterRangeStart, uint32 ClusterRangeNum, uint32 MeshIndex, FBounds& MeshBounds, TArray< int32 >* MipEnds)
{
	uint32 LevelOffset	= ClusterRangeStart;
	
	TAtomic< uint32 >	NumClusters( Clusters.Num() );
	uint32				NumExternalEdges = 0;

	bool bFirstLevel = true;

	while( true )
	{
		if( MipEnds )
		{
			MipEnds->Add( Clusters.Num() );
		}
		
		TArrayView< FCluster > LevelClusters( &Clusters[LevelOffset], bFirstLevel ? ClusterRangeNum : (Clusters.Num() - LevelOffset) );
		bFirstLevel = false;
		
		for( FCluster& Cluster : LevelClusters )
		{
			NumExternalEdges	+= Cluster.NumExternalEdges;
			MeshBounds			+= Cluster.Bounds;
		}

		if( LevelClusters.Num() < 2 )
			break;

		if( LevelClusters.Num() <= MaxGroupSize )
		{
			TArray< uint32, TInlineAllocator< MaxGroupSize > > Children;

			uint32 MaxParents = 0;
			for( FCluster& Cluster : LevelClusters )
			{
				MaxParents += FMath::DivideAndRoundUp< uint32 >( Cluster.Indexes.Num(), FCluster::ClusterSize * 6 );
				Children.Add( LevelOffset++ );
			}

			LevelOffset = Clusters.Num();
			Clusters.AddDefaulted( MaxParents );
			Groups.AddDefaulted( 1 );

			DAGReduce( Groups, Clusters, NumClusters, Children, Groups.Num() - 1, MeshIndex );

			// Correct num to atomic count
			Clusters.SetNum( NumClusters, false );

			continue;
		}
		
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
			});

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
			});

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

		LOG_CRC( Graph->Adjacency );
		LOG_CRC( Graph->AdjacencyCost );
		LOG_CRC( Graph->AdjacencyOffset );

		Partitioner.PartitionStrict( Graph, MinGroupSize, MaxGroupSize, true );

		LOG_CRC( Partitioner.Ranges );

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
		Groups.AddDefaulted( Partitioner.Ranges.Num() );

		ParallelFor( Partitioner.Ranges.Num(),
			[&]( int32 PartitionIndex )
			{
				auto& Range = Partitioner.Ranges[ PartitionIndex ];

				TArrayView< uint32 > Children( &Partitioner.Indexes[ Range.Begin ], Range.End - Range.Begin );
				uint32 ClusterGroupIndex = PartitionIndex + Groups.Num() - Partitioner.Ranges.Num();

				DAGReduce( Groups, Clusters, NumClusters, Children, ClusterGroupIndex, MeshIndex );
			});

		// Correct num to atomic count
		Clusters.SetNum( NumClusters, false );
	}
	
	// Max out root node
	uint32 RootIndex = LevelOffset;
	FClusterGroup RootClusterGroup;
	RootClusterGroup.Children.Add( RootIndex );
	RootClusterGroup.Bounds = Clusters[ RootIndex ].SphereBounds;
	RootClusterGroup.LODBounds = FSphere( 0 );
	RootClusterGroup.MaxParentLODError = 1e10f;
	RootClusterGroup.MinLODError = -1.0f;
	RootClusterGroup.MipLevel = MAX_int32;
	RootClusterGroup.MeshIndex = MeshIndex;
	Clusters[ RootIndex ].GroupIndex = Groups.Num();
	Groups.Add( RootClusterGroup );
}

static void DAGReduce( TArray< FClusterGroup >& Groups, TArray< FCluster >& Clusters, TAtomic< uint32 >& NumClusters, TArrayView< uint32 > Children, int32 GroupIndex, uint32 MeshIndex )
{
	check( GroupIndex >= 0 );

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

	float ParentMaxLODError = 0.0f;

	for( int32 TargetClusterSize = FCluster::ClusterSize - 2; TargetClusterSize > FCluster::ClusterSize / 2; TargetClusterSize -= 2 )
	{
		int32 TargetNumTris = NumParents * TargetClusterSize;

		// Simplify
		ParentMaxLODError = Merged.Simplify( TargetNumTris );

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

	TArray< FSphere, TInlineAllocator<32> > Children_LODBounds;
	TArray< FSphere, TInlineAllocator<32> > Children_SphereBounds;
					
	// Force monotonic nesting.
	float ChildMinLODError = MAX_flt;
	for( int32 Child : Children )
	{
		bool bLeaf = Clusters[ Child ].EdgeLength < 0.0f;
		float LODError = Clusters[ Child ].LODError;

		Children_LODBounds.Add( Clusters[ Child ].LODBounds );
		Children_SphereBounds.Add( Clusters[ Child ].SphereBounds );
		ChildMinLODError = FMath::Min( ChildMinLODError, bLeaf ? -1.0f : LODError );
		ParentMaxLODError = FMath::Max( ParentMaxLODError, LODError );

		Clusters[ Child ].GroupIndex = GroupIndex;
		Groups[ GroupIndex ].Children.Add( Child );
		check( Groups[ GroupIndex ].Children.Num() <= MAX_CLUSTERS_PER_GROUP_TARGET );
	}
	
	FSphere	ParentLODBounds( Children_LODBounds.GetData(), Children_LODBounds.Num() );
	FSphere	ParentBounds( Children_SphereBounds.GetData(), Children_SphereBounds.Num() );

	// Force parents to have same LOD data. They are all dependent.
	for( int32 Parent = ParentStart; Parent < ParentEnd; Parent++ )
	{
		Clusters[ Parent ].LODBounds			= ParentLODBounds;
		Clusters[ Parent ].LODError				= ParentMaxLODError;
		Clusters[ Parent ].GeneratingGroupIndex = GroupIndex;
	}

	Groups[ GroupIndex ].Bounds				= ParentBounds;
	Groups[ GroupIndex ].LODBounds			= ParentLODBounds;
	Groups[ GroupIndex ].MinLODError		= ChildMinLODError;
	Groups[ GroupIndex ].MaxParentLODError	= ParentMaxLODError;
	Groups[ GroupIndex ].MipLevel			= Merged.MipLevel;
	Groups[ GroupIndex ].MeshIndex			= MeshIndex;
}


FArchive& operator<<(FArchive& Ar, FClusterGroup& Group)
{
	Ar << Group.Bounds;
	Ar << Group.LODBounds;
	Ar << Group.MinLODError;
	Ar << Group.MaxParentLODError;
	Ar << Group.MipLevel;
	Ar << Group.MeshIndex;

	Ar << Group.PageIndexStart;
	Ar << Group.PageIndexNum;
	Ar << Group.Children;
	return Ar;
}

} // namespace Nanite