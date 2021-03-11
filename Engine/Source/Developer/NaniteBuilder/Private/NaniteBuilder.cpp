// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteBuilder.h"
#include "Modules/ModuleManager.h"
#include "Components.h"
#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "Hash/CityHash.h"
#include "Math/UnrealMath.h"
#include "GraphPartitioner.h"
#include "Cluster.h"
#include "ClusterDAG.h"
#include "MeshSimplify.h"
#include "DisjointSet.h"
#include "Async/ParallelFor.h"
#include "NaniteEncode.h"
#include "ImposterAtlas.h"

#if WITH_MIKKTSPACE
#include "mikktspace.h"
#endif

// If static mesh derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.
#define NANITE_DERIVEDDATA_VER TEXT("3ABB0516-8F1E-59F6-B3CA-91FDDBF10CB5")

namespace Nanite
{

class FBuilderModule : public IBuilderModule
{
public:
	FBuilderModule() {}

	virtual void StartupModule() override
	{
		// Register any modular features here
	}

	virtual void ShutdownModule() override
	{
		// Unregister any modular features here
	}

	virtual const FString& GetVersionString() const;

	virtual bool Build(
		FResources& Resources,
		TArray<FStaticMeshBuildVertex>& Vertices, // TODO: Do not require this vertex type for all users of Nanite
		TArray<uint32>& TriangleIndices,
		TArray<int32>& MaterialIndices,
		TArray<uint32>& MeshTriangleCounts,
		uint32 NumTexCoords,
		const FMeshNaniteSettings& Settings) override;

	virtual bool Build(
		FResources& Resources,
		TArray< FStaticMeshBuildVertex>& Vertices,
		TArray< uint32 >& TriangleIndices,
		TArray< FStaticMeshSection, TInlineAllocator<1>>& Sections,
		uint32 NumTexCoords,
		const FMeshNaniteSettings& Settings) override;
};

const FString& FBuilderModule::GetVersionString() const
{
	static FString VersionString;

	if (VersionString.IsEmpty())
	{
		VersionString = FString::Printf(TEXT("%s%s%s"), NANITE_DERIVEDDATA_VER,
										USE_CONSTRAINED_CLUSTERS ? TEXT("_CONSTRAINED") : TEXT(""),
										USE_UNCOMPRESSED_VERTEX_DATA ? TEXT("_UNCOMPRESSED") : TEXT(""));
	}

	return VersionString;
}

} // namespace Nanite

IMPLEMENT_MODULE( Nanite::FBuilderModule, NaniteBuilder );



namespace Nanite
{

struct FMeshData
{
	TArray< FStaticMeshBuildVertex >&	Verts;
	TArray< uint32 >&					Indexes;
};

static int MikkGetNumFaces( const SMikkTSpaceContext* Context )
{
	FMeshData *UserData = (FMeshData*)Context->m_pUserData;
	return UserData->Indexes.Num() / 3;
}

static int MikkGetNumVertsOfFace( const SMikkTSpaceContext* Context, const int FaceIdx )
{
	return 3;
}

static void MikkGetPosition( const SMikkTSpaceContext* Context, float Position[3], const int FaceIdx, const int VertIdx )
{
	FMeshData *UserData = (FMeshData*)Context->m_pUserData;
	for( int32 i = 0; i < 3; i++ )
		Position[i] = UserData->Verts[ UserData->Indexes[ FaceIdx * 3 + VertIdx ] ].Position[i];
}

static void MikkGetNormal( const SMikkTSpaceContext* Context, float Normal[3], const int FaceIdx, const int VertIdx )
{
	FMeshData *UserData = (FMeshData*)Context->m_pUserData;
	for( int32 i = 0; i < 3; i++ )
		Normal[i] = UserData->Verts[ UserData->Indexes[ FaceIdx * 3 + VertIdx ] ].TangentZ[i];
}

static void MikkSetTSpaceBasic( const SMikkTSpaceContext* Context, const float Tangent[3], const float BitangentSign, const int FaceIdx, const int VertIdx )
{
	FMeshData *UserData = (FMeshData*)Context->m_pUserData;
	for( int32 i = 0; i < 3; i++ )
		UserData->Verts[ UserData->Indexes[ FaceIdx * 3 + VertIdx ] ].TangentX[i] = Tangent[i];

	FVector Bitangent = BitangentSign * FVector::CrossProduct(
		UserData->Verts[ UserData->Indexes[ FaceIdx * 3 + VertIdx ] ].TangentZ,
		UserData->Verts[ UserData->Indexes[ FaceIdx * 3 + VertIdx ] ].TangentX );

	for( int32 i = 0; i < 3; i++ )
		UserData->Verts[ UserData->Indexes[ FaceIdx * 3 + VertIdx ] ].TangentY[i] = -Bitangent[i];
}

static void MikkGetTexCoord( const SMikkTSpaceContext* Context, float UV[2], const int FaceIdx, const int VertIdx )
{
	FMeshData *UserData = (FMeshData*)Context->m_pUserData;
	for( int32 i = 0; i < 2; i++ )
		UV[i] = UserData->Verts[ UserData->Indexes[ FaceIdx * 3 + VertIdx ] ].UVs[0][i];
}

void CalcTangents(
	TArray< FStaticMeshBuildVertex >& Verts,
	TArray< uint32 >& Indexes )
{
#if WITH_MIKKTSPACE
	FMeshData MeshData = { Verts, Indexes };

	SMikkTSpaceInterface MikkTInterface;
	MikkTInterface.m_getNormal				= MikkGetNormal;
	MikkTInterface.m_getNumFaces			= MikkGetNumFaces;
	MikkTInterface.m_getNumVerticesOfFace	= MikkGetNumVertsOfFace;
	MikkTInterface.m_getPosition			= MikkGetPosition;
	MikkTInterface.m_getTexCoord			= MikkGetTexCoord;
	MikkTInterface.m_setTSpaceBasic			= MikkSetTSpaceBasic;
	MikkTInterface.m_setTSpace				= nullptr;

	SMikkTSpaceContext MikkTContext;
	MikkTContext.m_pInterface				= &MikkTInterface;
	MikkTContext.m_pUserData				= (void*)(&MeshData);
	MikkTContext.m_bIgnoreDegenerates		= true;
	genTangSpaceDefault( &MikkTContext );
#else
	ensureMsgf(false, TEXT("MikkTSpace tangent generation is not supported on this platform."));
#endif //WITH_MIKKTSPACE
}

static void BuildCoarseRepresentation(
	const TArray<FClusterGroup>& Groups,
	const TArray<FCluster>& Clusters,
	TArray<FStaticMeshBuildVertex>& Verts,
	TArray<uint32>& Indexes,
	TArray<FStaticMeshSection, TInlineAllocator<1>>& Sections,
	uint32& NumTexCoords,
	uint32 TargetNumTris
)
{
	FCluster CoarseRepresentation = FindDAGCut(Groups, Clusters, TargetNumTris + 4096);

	CoarseRepresentation.Simplify(TargetNumTris);

	TArray< FStaticMeshSection, TInlineAllocator<1> > OldSections = Sections;

	// Need to update coarse representation UV count to match new data.
	NumTexCoords = CoarseRepresentation.NumTexCoords;

	// Rebuild vertex data
	Verts.Empty(CoarseRepresentation.NumVerts);
	for (uint32 Iter = 0, Num = CoarseRepresentation.NumVerts; Iter < Num; ++Iter)
	{
		FStaticMeshBuildVertex Vertex = {};
		Vertex.Position = CoarseRepresentation.GetPosition(Iter);
		Vertex.TangentX = FVector::ZeroVector;
		Vertex.TangentY = FVector::ZeroVector;
		Vertex.TangentZ = CoarseRepresentation.GetNormal(Iter);

		const FVector2D* UVs = CoarseRepresentation.GetUVs(Iter);
		for (uint32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
		{
			Vertex.UVs[UVIndex] = UVs[UVIndex].ContainsNaN() ? FVector2D::ZeroVector : UVs[UVIndex];
		}

		if (CoarseRepresentation.bHasColors)
		{
			Vertex.Color = CoarseRepresentation.GetColor(Iter).ToFColor(false /* sRGB */);
		}

		Verts.Add(Vertex);
	}

	TArray<FMaterialTriangle, TInlineAllocator<128>> CoarseMaterialTris;
	TArray<FMaterialRange, TInlineAllocator<4>> CoarseMaterialRanges;

	// Compute material ranges for coarse representation.
	BuildMaterialRanges(
		CoarseRepresentation.Indexes,
		CoarseRepresentation.MaterialIndexes,
		CoarseMaterialTris,
		CoarseMaterialRanges);
	check(CoarseMaterialRanges.Num() <= OldSections.Num());

	// Rebuild section data.
	Sections.Reset(CoarseMaterialRanges.Num());
	for (const FStaticMeshSection& OldSection : OldSections)
	{
		// Add new sections based on the computed material ranges
		// Enforce the same material order as OldSections
		const FMaterialRange* FoundRange = CoarseMaterialRanges.FindByPredicate([&OldSection](const FMaterialRange& Range) { return Range.MaterialIndex == OldSection.MaterialIndex; });

		// Sections can actually be removed from the coarse mesh if their source data doesn't contain enough triangles
		if (FoundRange)
		{
			// Copy properties from original mesh sections.
			FStaticMeshSection Section(OldSection);

			// Range of vertices and indices used when rendering this section.
			Section.FirstIndex = FoundRange->RangeStart * 3;
			Section.NumTriangles = FoundRange->RangeLength;
			Section.MinVertexIndex = TNumericLimits<uint32>::Max();
			Section.MaxVertexIndex = TNumericLimits<uint32>::Min();

			for (uint32 TriangleIndex = 0; TriangleIndex < (FoundRange->RangeStart + FoundRange->RangeLength); ++TriangleIndex)
			{
				const FMaterialTriangle& Triangle = CoarseMaterialTris[TriangleIndex];

				// Update min vertex index
				Section.MinVertexIndex = FMath::Min(Section.MinVertexIndex, Triangle.Index0);
				Section.MinVertexIndex = FMath::Min(Section.MinVertexIndex, Triangle.Index1);
				Section.MinVertexIndex = FMath::Min(Section.MinVertexIndex, Triangle.Index2);

				// Update max vertex index
				Section.MaxVertexIndex = FMath::Max(Section.MaxVertexIndex, Triangle.Index0);
				Section.MaxVertexIndex = FMath::Max(Section.MaxVertexIndex, Triangle.Index1);
				Section.MaxVertexIndex = FMath::Max(Section.MaxVertexIndex, Triangle.Index2);
			}

			Sections.Add(Section);
		}
	}

	// Rebuild index data.
	Indexes.Reset();
	for (const FMaterialTriangle& Triangle : CoarseMaterialTris)
	{
		Indexes.Add(Triangle.Index0);
		Indexes.Add(Triangle.Index1);
		Indexes.Add(Triangle.Index2);
	}

	CalcTangents(Verts, Indexes);
}

static void ClusterTriangles(
	const TArray< FStaticMeshBuildVertex >& Verts,
	const TArrayView< const uint32 >& Indexes,
	const TArrayView< const int32 >& MaterialIndexes,
	TArray< FCluster >& Clusters,	// Append
	const FBounds& MeshBounds,
	uint32 NumTexCoords,
	bool bHasColors )
{
	uint32 Time0 = FPlatformTime::Cycles();

	LOG_CRC( Verts );
	LOG_CRC( Indexes );

	uint32 NumTriangles = Indexes.Num() / 3;

	TArray< uint32 > SharedEdges;
	SharedEdges.AddUninitialized( Indexes.Num() );

	TBitArray<> BoundaryEdges;
	BoundaryEdges.Init( false, Indexes.Num() );

	FHashTable EdgeHash( 1 << FMath::FloorLog2( Indexes.Num() ), Indexes.Num() );

	ParallelFor( Indexes.Num(),
		[&]( int32 EdgeIndex )
		{

			uint32 VertIndex0 = Indexes[ EdgeIndex ];
			uint32 VertIndex1 = Indexes[ Cycle3( EdgeIndex ) ];
	
			const FVector& Position0 = Verts[ VertIndex0 ].Position;
			const FVector& Position1 = Verts[ VertIndex1 ].Position;
				
			uint32 Hash0 = HashPosition( Position0 );
			uint32 Hash1 = HashPosition( Position1 );
			uint32 Hash = Murmur32( { Hash0, Hash1 } );

			EdgeHash.Add_Concurrent( Hash, EdgeIndex );
		});

	const int32 NumDwords = FMath::DivideAndRoundUp( BoundaryEdges.Num(), NumBitsPerDWORD );

	ParallelFor( NumDwords,
		[&]( int32 DwordIndex )
		{
			const int32 NumIndexes = Indexes.Num();
			const int32 NumBits = FMath::Min( NumBitsPerDWORD, NumIndexes - DwordIndex * NumBitsPerDWORD );

			uint32 Mask = 1;
			uint32 Dword = 0;
			for( int32 BitIndex = 0; BitIndex < NumBits; BitIndex++, Mask <<= 1 )
			{
				int32 EdgeIndex = DwordIndex * NumBitsPerDWORD + BitIndex;

				uint32 VertIndex0 = Indexes[ EdgeIndex ];
				uint32 VertIndex1 = Indexes[ Cycle3( EdgeIndex ) ];
	
				const FVector& Position0 = Verts[ VertIndex0 ].Position;
				const FVector& Position1 = Verts[ VertIndex1 ].Position;
				
				uint32 Hash0 = HashPosition( Position0 );
				uint32 Hash1 = HashPosition( Position1 );
				uint32 Hash = Murmur32( { Hash1, Hash0 } );
	
				// Find edge with opposite direction that shares these 2 verts.
				/*
					  /\
					 /  \
					o-<<-o
					o->>-o
					 \  /
					  \/
				*/
				uint32 FoundEdge = ~0u;
				for( uint32 OtherEdgeIndex = EdgeHash.First( Hash ); EdgeHash.IsValid( OtherEdgeIndex ); OtherEdgeIndex = EdgeHash.Next( OtherEdgeIndex ) )
				{
					uint32 OtherVertIndex0 = Indexes[ OtherEdgeIndex ];
					uint32 OtherVertIndex1 = Indexes[ Cycle3( OtherEdgeIndex ) ];
			
					if( Position0 == Verts[ OtherVertIndex1 ].Position &&
						Position1 == Verts[ OtherVertIndex0 ].Position )
					{
						// Found matching edge.
						// Hash table is not in deterministic order. Find stable match not just first.
						FoundEdge = FMath::Min( FoundEdge, OtherEdgeIndex );
					}
				}
				SharedEdges[ EdgeIndex ] = FoundEdge;
			
				if( FoundEdge == ~0u )
				{
					Dword |= Mask;
				}
			}
			
			if( Dword )
			{
				BoundaryEdges.GetData()[ DwordIndex ] = Dword;
			}
		});

	FDisjointSet DisjointSet( NumTriangles );

	for( uint32 EdgeIndex = 0, Num = SharedEdges.Num(); EdgeIndex < Num; EdgeIndex++ )
	{
		uint32 OtherEdgeIndex = SharedEdges[ EdgeIndex ];
		if( OtherEdgeIndex != ~0u )
		{
			// OtherEdgeIndex is smallest that matches EdgeIndex
			// ThisEdgeIndex is smallest that matches OtherEdgeIndex

			uint32 ThisEdgeIndex = SharedEdges[ OtherEdgeIndex ];
			check( ThisEdgeIndex != ~0u );
			check( ThisEdgeIndex <= EdgeIndex );

			if( EdgeIndex > ThisEdgeIndex )
			{
				// Previous element points to OtherEdgeIndex
				SharedEdges[ EdgeIndex ] = ~0u;
			}
			else if( EdgeIndex > OtherEdgeIndex )
			{
				// Second time seeing this
				DisjointSet.UnionSequential( EdgeIndex / 3, OtherEdgeIndex / 3 );
			}
		}
	}

	uint32 BoundaryTime = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Boundary [%.2fs], tris: %i, UVs %i%s"), FPlatformTime::ToMilliseconds( BoundaryTime - Time0 ) / 1000.0f, Indexes.Num() / 3, NumTexCoords, bHasColors ? TEXT(", Color") : TEXT("") );

	LOG_CRC( SharedEdges );

	FGraphPartitioner Partitioner( NumTriangles );

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::PartitionGraph);

		auto GetCenter = [ &Verts, &Indexes ]( uint32 TriIndex )
		{
			FVector Center;
			Center  = Verts[ Indexes[ TriIndex * 3 + 0 ] ].Position;
			Center += Verts[ Indexes[ TriIndex * 3 + 1 ] ].Position;
			Center += Verts[ Indexes[ TriIndex * 3 + 2 ] ].Position;
			return Center * (1.0f / 3.0f);
		};
		Partitioner.BuildLocalityLinks( DisjointSet, MeshBounds, GetCenter );

		auto* RESTRICT Graph = Partitioner.NewGraph( NumTriangles * 3 );

		for( uint32 i = 0; i < NumTriangles; i++ )
		{
			Graph->AdjacencyOffset[i] = Graph->Adjacency.Num();

			uint32 TriIndex = Partitioner.Indexes[i];

			for( int k = 0; k < 3; k++ )
			{
				uint32 EdgeIndex = SharedEdges[ 3 * TriIndex + k ];
				if( EdgeIndex != ~0u )
				{
					Partitioner.AddAdjacency( Graph, EdgeIndex / 3, 4 * 65 );
				}
			}

			Partitioner.AddLocalityLinks( Graph, TriIndex, 1 );
		}
		Graph->AdjacencyOffset[ NumTriangles ] = Graph->Adjacency.Num();

		Partitioner.PartitionStrict( Graph, FCluster::ClusterSize - 4, FCluster::ClusterSize, true );
		check( Partitioner.Ranges.Num() );

		LOG_CRC( Partitioner.Ranges );
	}

	const uint32 OptimalNumClusters = FMath::DivideAndRoundUp< int32 >( Indexes.Num(), FCluster::ClusterSize * 3 );

	uint32 ClusterTime = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Clustering [%.2fs]. Ratio: %f"), FPlatformTime::ToMilliseconds( ClusterTime - BoundaryTime ) / 1000.0f, (float)Partitioner.Ranges.Num() / OptimalNumClusters );

	const uint32 BaseCluster = Clusters.Num();
	Clusters.AddDefaulted( Partitioner.Ranges.Num() );

	const bool bSingleThreaded = Partitioner.Ranges.Num() > 32;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::BuildClusters);
		ParallelFor( Partitioner.Ranges.Num(),
			[&]( int32 Index )
			{
				auto& Range = Partitioner.Ranges[ Index ];


				Clusters[ BaseCluster + Index ] = FCluster( Verts,
															Indexes,
															MaterialIndexes,
															BoundaryEdges, Range.Begin, Range.End, Partitioner.Indexes, NumTexCoords, bHasColors );

				// Negative notes it's a leaf
				Clusters[ BaseCluster + Index ].EdgeLength *= -1.0f;
			}, bSingleThreaded);
	}

	uint32 LeavesTime = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Leaves [%.2fs]"), FPlatformTime::ToMilliseconds( LeavesTime - ClusterTime ) / 1000.0f );
}

static bool BuildNaniteData(
	FResources& Resources,
	TArray< FStaticMeshBuildVertex >& Verts, // TODO: Do not require this vertex type for all users of Nanite
	TArray< uint32 >& Indexes,
	TArray< int32 >& MaterialIndexes,
	TArray<uint32>& MeshTriangleCounts,
	TArray< FStaticMeshSection, TInlineAllocator<1> >& Sections,
	uint32 NumTexCoords,
	const FMeshNaniteSettings& Settings
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::BuildData);

	if (NumTexCoords > MAX_NANITE_UVS) NumTexCoords = MAX_NANITE_UVS;

	FBounds	VertexBounds;
	uint32 Channel = 255;
	for( auto& Vert : Verts )
	{
		VertexBounds += Vert.Position;

		Channel &= Vert.Color.R;
		Channel &= Vert.Color.G;
		Channel &= Vert.Color.B;
		Channel &= Vert.Color.A;
	}

	const uint32 NumMeshes = MeshTriangleCounts.Num();
	
	// Don't trust any input. We only have color if it isn't all white.
	bool bHasColors = Channel != 255;

	TArray< uint32 > ClusterCountPerMesh;
	TArray< FCluster > Clusters;
	{
		uint32 BaseTriangle = 0;
		for (uint32 NumTriangles : MeshTriangleCounts)
		{
			uint32 NumClustersBefore = Clusters.Num();
			if (NumTriangles)
			{
				ClusterTriangles(Verts, TArrayView< const uint32 >( &Indexes[BaseTriangle * 3], NumTriangles * 3 ),
										TArrayView< const int32 >( &MaterialIndexes[BaseTriangle], NumTriangles ),
										Clusters, VertexBounds, NumTexCoords, bHasColors);
			}
			ClusterCountPerMesh.Add(Clusters.Num() - NumClustersBefore);
			BaseTriangle += NumTriangles;
		}
	}
	
	const int32 OldTriangleCount = Indexes.Num() / 3;
	const int32 MinTriCount = 2000;
	// Replace original static mesh data with coarse representation.
	const bool bUseCoarseRepresentation = Settings.PercentTriangles < 1.0f && OldTriangleCount > MinTriCount;

	// If we're going to replace the original vertex buffer with a coarse representation, get rid of the old copies
	// now that we copied it into the cluster representation. We do it before the longer DAG reduce phase to shorten peak memory duration.
	// This is especially important when building multiple huge Nanite meshes in parallel.
	if (bUseCoarseRepresentation)
	{
		check(MeshTriangleCounts.Num() == 1);
		Verts.Empty();
		Indexes.Empty();
		MaterialIndexes.Empty();
	}

	uint32 Time0 = FPlatformTime::Cycles();

	FBounds MeshBounds;
	TArray<FClusterGroup> Groups;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::DAG.Reduce);
		
		uint32 ClusterStart = 0;
		for (uint32 MeshIndex = 0; MeshIndex < NumMeshes; MeshIndex++)
		{
			uint32 NumClusters = ClusterCountPerMesh[MeshIndex];
			BuildDAG( Groups, Clusters, ClusterStart, NumClusters, MeshIndex, MeshBounds );
			ClusterStart += NumClusters;
		}
	}

	uint32 ReduceTime = FPlatformTime::Cycles();
	UE_LOG(LogStaticMesh, Log, TEXT("Reduce [%.2fs]"), FPlatformTime::ToMilliseconds(ReduceTime - Time0) / 1000.0f);

	if (bUseCoarseRepresentation)
	{
		const uint32 CoarseStartTime = FPlatformTime::Cycles();
		int32 CoarseTriCount = FMath::Max(MinTriCount, int32((float(OldTriangleCount) * Settings.PercentTriangles)));

		TArray<FStaticMeshSection, TInlineAllocator<1>> CoarseSections = Sections;
		BuildCoarseRepresentation(Groups, Clusters, Verts, Indexes, CoarseSections, NumTexCoords, CoarseTriCount);

		// Fixup mesh section info with new coarse mesh ranges, while respecting original ordering and keeping materials
		// that do not end up with any assigned triangles (due to decimation process).

		for (FStaticMeshSection& Section : Sections)
		{
			// For each section info, try to find a matching entry in the coarse version.
			const FStaticMeshSection* CoarseSection = CoarseSections.FindByPredicate(
				[&Section](const FStaticMeshSection& CoarseSectionIter)
			{
				return CoarseSectionIter.MaterialIndex == Section.MaterialIndex;
			});

			if (CoarseSection != nullptr)
			{
				// Matching entry found
				Section.FirstIndex     = CoarseSection->FirstIndex;
				Section.NumTriangles   = CoarseSection->NumTriangles;
				Section.MinVertexIndex = CoarseSection->MinVertexIndex;
				Section.MaxVertexIndex = CoarseSection->MaxVertexIndex;
			}
			else
			{
				// Section removed due to decimation, set placeholder entry
				Section.FirstIndex     = 0;
				Section.NumTriangles   = 0;
				Section.MinVertexIndex = 0;
				Section.MaxVertexIndex = 0;
			}
		}

		const uint32 CoarseEndTime = FPlatformTime::Cycles();
		UE_LOG(LogStaticMesh, Log, TEXT("Coarse [%.2fs], original tris: %d, coarse tris: %d"), FPlatformTime::ToMilliseconds(CoarseEndTime - CoarseStartTime) / 1000.0f, OldTriangleCount, CoarseTriCount);
	}

	uint32 EncodeTime0 = FPlatformTime::Cycles();

	Encode( Resources, Clusters, Groups, MeshBounds, NumMeshes, NumTexCoords, bHasColors );

	uint32 EncodeTime1 = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Encode [%.2fs]"), FPlatformTime::ToMilliseconds( EncodeTime1 - EncodeTime0 ) / 1000.0f );

	const bool bGenerateImposter = (NumMeshes == 1);
	if (bGenerateImposter)
	{
		uint32 ImposterStartTime = FPlatformTime::Cycles();
		auto& RootChildren = Groups.Last().Children;
	
		FImposterAtlas ImposterAtlas( Resources.ImposterAtlas, MeshBounds );

		ParallelFor(FMath::Square(FImposterAtlas::AtlasSize),
			[&](int32 TileIndex)
		{
			FIntPoint TilePos(
				TileIndex % FImposterAtlas::AtlasSize,
				TileIndex / FImposterAtlas::AtlasSize);

			for (int32 ClusterIndex = 0; ClusterIndex < RootChildren.Num(); ClusterIndex++)
			{
				ImposterAtlas.Rasterize(TilePos, Clusters[RootChildren[ClusterIndex]], ClusterIndex);
			}
		});

		UE_LOG(LogStaticMesh, Log, TEXT("Imposter [%.2fs]"), FPlatformTime::ToMilliseconds(FPlatformTime::Cycles() - ImposterStartTime ) / 1000.0f);
	}

	uint32 Time1 = FPlatformTime::Cycles();

	UE_LOG( LogStaticMesh, Log, TEXT("Nanite build [%.2fs]\n"), FPlatformTime::ToMilliseconds( Time1 - Time0 ) / 1000.0f );

	return true;
}

bool FBuilderModule::Build(
	FResources& Resources,
	TArray<FStaticMeshBuildVertex>& Vertices, // TODO: Do not require this vertex type for all users of Nanite
	TArray<uint32>& TriangleIndices,
	TArray<int32>&  MaterialIndices,
	TArray<uint32>& MeshTriangleCounts,
	uint32 NumTexCoords,
	const FMeshNaniteSettings& Settings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build);

	check(Settings.PercentTriangles == 1.0f); // No coarse representation used by this path
	TArray<FStaticMeshSection, TInlineAllocator<1>> IgnoredCoarseSections;
	return BuildNaniteData(
		Resources,
		Vertices,
		TriangleIndices,
		MaterialIndices,
		MeshTriangleCounts,
		IgnoredCoarseSections,
		NumTexCoords,
		Settings
	);
}

bool FBuilderModule::Build(
	FResources& Resources,
	TArray< FStaticMeshBuildVertex>& Vertices,
	TArray< uint32 >& TriangleIndices,
	TArray< FStaticMeshSection, TInlineAllocator<1>>& Sections,
	uint32 NumTexCoords,
	const FMeshNaniteSettings& Settings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build);

	// TODO: Properly error out if # of unique materials is > 64 (error message to editor log)
	check(Sections.Num() > 0 && Sections.Num() <= 64);

	// Build associated array of triangle index and material index.
	TArray<int32> MaterialIndices;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::BuildSections);
		MaterialIndices.Reserve(TriangleIndices.Num() / 3);
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); SectionIndex++)
		{
			FStaticMeshSection& Section = Sections[SectionIndex];

			// TODO: Safe to enforce valid materials always?
			check(Section.MaterialIndex != INDEX_NONE);
			for (uint32 i = 0; i < Section.NumTriangles; ++i)
			{
				MaterialIndices.Add(Section.MaterialIndex);
			}
		}
	}

	TArray<uint32> MeshTriangleCounts;
	MeshTriangleCounts.Add(TriangleIndices.Num() / 3);

	// Make sure there is 1 material index per triangle.
	check(MaterialIndices.Num() * 3 == TriangleIndices.Num());

	return BuildNaniteData(
		Resources,
		Vertices,
		TriangleIndices,
		MaterialIndices,
		MeshTriangleCounts,
		Sections,
		NumTexCoords,
		Settings
	);
}

} // namespace Nanite
