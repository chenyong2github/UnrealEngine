// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cluster.h"
#include "GraphPartitioner.h"

template< typename T > FORCEINLINE uint32 Min3Index( const T A, const T B, const T C ) { return ( A < B ) ? ( ( A < C ) ? 0 : 2 ) : ( ( B < C ) ? 1 : 2 ); }
template< typename T > FORCEINLINE uint32 Max3Index( const T A, const T B, const T C ) { return ( A > B ) ? ( ( A > C ) ? 0 : 2 ) : ( ( B > C ) ? 1 : 2 ); }

namespace Nanite
{

void CorrectAttributes( float* Attributes )
{
	FVector3f& Normal = *reinterpret_cast< FVector3f* >( Attributes );
	Normal.Normalize();
}

void CorrectAttributesColor( float* Attributes )
{
	CorrectAttributes( Attributes );
	
	FLinearColor& Color = *reinterpret_cast< FLinearColor* >( Attributes + 3 );
	Color = Color.GetClamped();
}


FCluster::FCluster(
	const TArray< FStaticMeshBuildVertex >& InVerts,
	const TArrayView< const uint32 >& InIndexes,
	const TArrayView< const int32 >& InMaterialIndexes,
	uint32 InNumTexCoords, bool bInHasColors,
	uint32 TriBegin, uint32 TriEnd, const FGraphPartitioner& Partitioner, const FAdjacency& Adjacency )
{
	GUID = (uint64(TriBegin) << 32) | TriEnd;
	
	NumTris = TriEnd - TriBegin;
	//ensure(NumTriangles <= FCluster::ClusterSize);
	
	bHasColors = bInHasColors;
	NumTexCoords = InNumTexCoords;

	Verts.Reserve( NumTris * GetVertSize() );
	Indexes.Reserve( 3 * NumTris );
	MaterialIndexes.Reserve( NumTris );
	ExternalEdges.Reserve( 3 * NumTris );
	NumExternalEdges = 0;

	check(InMaterialIndexes.Num() * 3 == InIndexes.Num());

	TMap< uint32, uint32 > OldToNewIndex;
	OldToNewIndex.Reserve( NumTris );

	for( uint32 i = TriBegin; i < TriEnd; i++ )
	{
		uint32 TriIndex = Partitioner.Indexes[i];

		for( uint32 k = 0; k < 3; k++ )
		{
			uint32 OldIndex = InIndexes[ TriIndex * 3 + k ];
			uint32* NewIndexPtr = OldToNewIndex.Find( OldIndex );
			uint32 NewIndex = NewIndexPtr ? *NewIndexPtr : ~0u;

			if( NewIndex == ~0u )
			{
				Verts.AddUninitialized( GetVertSize() );
				NewIndex = NumVerts++;
				OldToNewIndex.Add( OldIndex, NewIndex );
				
				const FStaticMeshBuildVertex& InVert = InVerts[ OldIndex ];

				GetPosition( NewIndex ) = InVert.Position;
				GetNormal( NewIndex ) = InVert.TangentZ;
	
				if( bHasColors )
				{
					GetColor( NewIndex ) = InVert.Color.ReinterpretAsLinear();
				}

				FVector2f* UVs = GetUVs( NewIndex );
				for( uint32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
				{
					UVs[ UVIndex ] = InVert.UVs[ UVIndex ];
				}
			}

			Indexes.Add( NewIndex );

			int32 EdgeIndex = TriIndex * 3 + k;
			int32 AdjCount = 0;
			
			Adjacency.ForAll( EdgeIndex,
				[ &AdjCount, TriBegin, TriEnd, &Partitioner ]( int32 EdgeIndex, int32 AdjIndex )
				{
					uint32 AdjTri = Partitioner.SortedTo[ AdjIndex / 3 ];
					if( AdjTri < TriBegin || AdjTri >= TriEnd )
						AdjCount++;
				} );

			ExternalEdges.Add( AdjCount );
			NumExternalEdges += AdjCount != 0 ? 1 : 0;
		}

		MaterialIndexes.Add( InMaterialIndexes[ TriIndex ] );
	}

	SanitizeVertexData();

	for( uint32 VertexIndex = 0; VertexIndex < NumVerts; VertexIndex++ )
	{
		float* Attributes = GetAttributes( VertexIndex );

		// Make sure this vertex is valid from the start
		if( bHasColors )
			CorrectAttributesColor( Attributes );
		else
			CorrectAttributes( Attributes );
	}

	Bound();
}

// Split
FCluster::FCluster( FCluster& SrcCluster, uint32 TriBegin, uint32 TriEnd, const FGraphPartitioner& Partitioner, const FAdjacency& Adjacency )
	: MipLevel( SrcCluster.MipLevel )
{
	GUID = MurmurFinalize64(SrcCluster.GUID) ^ ((uint64(TriBegin) << 32) | TriEnd);

	NumTexCoords = SrcCluster.NumTexCoords;
	bHasColors   = SrcCluster.bHasColors;
	
	NumTris = TriEnd - TriBegin;

	Verts.Reserve( NumTris * GetVertSize() );
	Indexes.Reserve( 3 * NumTris );
	MaterialIndexes.Reserve( NumTris );
	ExternalEdges.Reserve( 3 * NumTris );
	NumExternalEdges = 0;

	TMap< uint32, uint32 > OldToNewIndex;
	OldToNewIndex.Reserve( NumTris );

	for( uint32 i = TriBegin; i < TriEnd; i++ )
	{
		uint32 TriIndex = Partitioner.Indexes[i];

		for( uint32 k = 0; k < 3; k++ )
		{
			uint32 OldIndex = SrcCluster.Indexes[ TriIndex * 3 + k ];
			uint32* NewIndexPtr = OldToNewIndex.Find( OldIndex );
			uint32 NewIndex = NewIndexPtr ? *NewIndexPtr : ~0u;

			if( NewIndex == ~0u )
			{
				Verts.AddUninitialized( GetVertSize() );
				NewIndex = NumVerts++;
				OldToNewIndex.Add( OldIndex, NewIndex );

				FMemory::Memcpy( &GetPosition( NewIndex ), &SrcCluster.GetPosition( OldIndex ), GetVertSize() * sizeof( float ) );
			}

			Indexes.Add( NewIndex );

			int32 EdgeIndex = TriIndex * 3 + k;
			int32 AdjCount = SrcCluster.ExternalEdges[ EdgeIndex ];
			
			Adjacency.ForAll( EdgeIndex,
				[ &AdjCount, TriBegin, TriEnd, &Partitioner ]( int32 EdgeIndex, int32 AdjIndex )
				{
					uint32 AdjTri = Partitioner.SortedTo[ AdjIndex / 3 ];
					if( AdjTri < TriBegin || AdjTri >= TriEnd )
						AdjCount++;
				} );

			ExternalEdges.Add( AdjCount );
			NumExternalEdges += AdjCount != 0 ? 1 : 0;
		}

		const int32 MaterialIndex = SrcCluster.MaterialIndexes[ TriIndex ];
		MaterialIndexes.Add( MaterialIndex );
	}

	Bound();
}

// Merge
FCluster::FCluster( const TArray< const FCluster*, TInlineAllocator<32> >& MergeList )
{
	NumTexCoords = MergeList[0]->NumTexCoords;
	bHasColors = MergeList[0]->bHasColors;

	const uint32 NumTrisGuess = ClusterSize * MergeList.Num();

	Verts.Reserve( NumTrisGuess * GetVertSize() );
	Indexes.Reserve( 3 * NumTrisGuess );
	MaterialIndexes.Reserve( NumTrisGuess );
	ExternalEdges.Reserve( 3 * NumTrisGuess );
	NumExternalEdges = 0;

	FHashTable VertHashTable( 1 << FMath::FloorLog2( NumTrisGuess ), NumTrisGuess );

	for( const FCluster* Child : MergeList )
	{
		Bounds += Child->Bounds;
		SurfaceArea += Child->SurfaceArea;

		// Can jump multiple levels but guarantee it steps at least 1.
		MipLevel = FMath::Max( MipLevel, Child->MipLevel + 1 );

		for( int32 i = 0; i < Child->Indexes.Num(); i++ )
		{
			uint32 NewIndex = AddVert( &Child->Verts[ Child->Indexes[i] * GetVertSize() ], VertHashTable );

			Indexes.Add( NewIndex );
			ExternalEdges.Add( Child->ExternalEdges[i] );
		}

		for( int32 i = 0; i < Child->MaterialIndexes.Num(); i++ )
		{
			const int32 MaterialIndex = Child->MaterialIndexes[i];
			MaterialIndexes.Add( MaterialIndex );
		}

		GUID = MurmurFinalize64(GUID) ^ Child->GUID;
	}

	FAdjacency Adjacency = BuildAdjacency();

	int32 ChildIndex = 0;
	int32 MinIndex = 0;
	int32 MaxIndex = MergeList[0]->ExternalEdges.Num();

	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		if( EdgeIndex >= MaxIndex )
		{
			ChildIndex++;
			MinIndex = MaxIndex;
			MaxIndex += MergeList[ ChildIndex ]->ExternalEdges.Num();
		}

		int32 AdjCount = ExternalEdges[ EdgeIndex ];

		Adjacency.ForAll( EdgeIndex,
			[ &AdjCount, MinIndex, MaxIndex ]( int32 EdgeIndex, int32 AdjIndex )
			{
				if( AdjIndex < MinIndex || AdjIndex >= MaxIndex )
					AdjCount--;
			} );

		// This seems like a sloppy workaround for a bug elsewhere but it is possible an interior edge is moved during simplifiation to
		// match another cluster and it isn't reflected in this count. Sounds unlikely but any hole closing could do this.
		// The only way to catch it would be to rebuild full adjacency after every pass which isn't practical.
		AdjCount = FMath::Max( AdjCount, 0 );

		ExternalEdges[ EdgeIndex ] = AdjCount;
		NumExternalEdges += AdjCount != 0 ? 1 : 0;
	}

	NumTris = Indexes.Num() / 3;
}

float FCluster::Simplify( uint32 TargetNumTris, float TargetError, uint32 LimitNumTris )
{
	if( ( TargetNumTris >= NumTris && TargetError == 0.0f ) || LimitNumTris >= NumTris )
	{
		return 0.0f;
	}

	float UVArea[ MAX_STATIC_TEXCOORDS ] = { 0.0f };

	for( uint32 TriIndex = 0; TriIndex < NumTris; TriIndex++ )
	{
		uint32 Index0 = Indexes[ TriIndex * 3 + 0 ];
		uint32 Index1 = Indexes[ TriIndex * 3 + 1 ];
		uint32 Index2 = Indexes[ TriIndex * 3 + 2 ];

		FVector2f* UV0 = GetUVs( Index0 );
		FVector2f* UV1 = GetUVs( Index1 );
		FVector2f* UV2 = GetUVs( Index2 );

		for( uint32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
		{
			FVector2f EdgeUV1 = UV1[ UVIndex ] - UV0[ UVIndex ];
			FVector2f EdgeUV2 = UV2[ UVIndex ] - UV0[ UVIndex ];
			float SignedArea = 0.5f * ( EdgeUV1 ^ EdgeUV2 );
			UVArea[ UVIndex ] += FMath::Abs( SignedArea );

			// Force an attribute discontinuity for UV mirroring edges.
			// Quadric could account for this but requires much larger UV weights which raises error on meshes which have no visible issues otherwise.
			MaterialIndexes[ TriIndex ] |= ( SignedArea >= 0.0f ? 1 : 0 ) << ( UVIndex + 24 );
		}
	}

	float TriangleSize = FMath::Sqrt( SurfaceArea / NumTris );
	
	FFloat32 CurrentSize( FMath::Max( TriangleSize, THRESH_POINTS_ARE_SAME ) );
	FFloat32 DesiredSize( 0.25f );
	FFloat32 FloatScale( 1.0f );

	// Lossless scaling by only changing the float exponent.
	int32 Exponent = FMath::Clamp( (int)DesiredSize.Components.Exponent - (int)CurrentSize.Components.Exponent, -126, 127 );
	FloatScale.Components.Exponent = Exponent + 127;	//ExpBias
	// Scale ~= DesiredSize / CurrentSize
	float PositionScale = FloatScale.FloatValue;

	for( uint32 i = 0; i < NumVerts; i++ )
	{
		GetPosition(i) *= PositionScale;
	}
	TargetError *= PositionScale;

	uint32 NumAttributes = GetVertSize() - 3;
	float* AttributeWeights = (float*)FMemory_Alloca( NumAttributes * sizeof( float ) );

	// Normal
	AttributeWeights[0] = 1.0f;
	AttributeWeights[1] = 1.0f;
	AttributeWeights[2] = 1.0f;

	if( bHasColors )
	{
		float* ColorWeights = AttributeWeights + 3;
		ColorWeights[0] = 0.0625f;
		ColorWeights[1] = 0.0625f;
		ColorWeights[2] = 0.0625f;
		ColorWeights[3] = 0.0625f;
	}
	
	uint32 TexCoordOffset = 3 + ( bHasColors ? 4 : 0 );
	float* UVWeights = AttributeWeights + TexCoordOffset;

	// Normalize UVWeights
	for( uint32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
	{
		float TriangleUVSize = FMath::Sqrt( UVArea[ UVIndex ] / NumTris );
		TriangleUVSize = FMath::Max( TriangleUVSize, THRESH_UVS_ARE_SAME );

		UVWeights[ 2 * UVIndex + 0 ] = 1.0f / ( 128.0f * TriangleUVSize );
		UVWeights[ 2 * UVIndex + 1 ] = 1.0f / ( 128.0f * TriangleUVSize );
	}

	FMeshSimplifier Simplifier( Verts.GetData(), NumVerts, Indexes.GetData(), Indexes.Num(), MaterialIndexes.GetData(), NumAttributes );

	TMap< TTuple< FVector3f, FVector3f >, int8 > LockedEdges;

	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		if( ExternalEdges[ EdgeIndex ] )
		{
			uint32 VertIndex0 = Indexes[ EdgeIndex ];
			uint32 VertIndex1 = Indexes[ Cycle3( EdgeIndex ) ];
	
			const FVector3f& Position0 = GetPosition( VertIndex0 );
			const FVector3f& Position1 = GetPosition( VertIndex1 );

			Simplifier.LockPosition( Position0 );
			Simplifier.LockPosition( Position1 );

			LockedEdges.Add( MakeTuple( Position0, Position1 ), ExternalEdges[ EdgeIndex ] );
		}
	}

	Simplifier.SetAttributeWeights( AttributeWeights );
	Simplifier.SetCorrectAttributes( bHasColors ? CorrectAttributesColor : CorrectAttributes );
	Simplifier.SetEdgeWeight( 2.0f );

	float MaxErrorSqr = Simplifier.Simplify(
		NumVerts, TargetNumTris, FMath::Square( TargetError ),
		0, LimitNumTris, MAX_flt );

	check( Simplifier.GetRemainingNumVerts() > 0 );
	check( Simplifier.GetRemainingNumTris() > 0 );

	Simplifier.Compact();
	
	Verts.SetNum( Simplifier.GetRemainingNumVerts() * GetVertSize() );
	Indexes.SetNum( Simplifier.GetRemainingNumTris() * 3 );
	MaterialIndexes.SetNum( Simplifier.GetRemainingNumTris() );
	ExternalEdges.Init( 0, Simplifier.GetRemainingNumTris() * 3 );

	NumVerts = Simplifier.GetRemainingNumVerts();
	NumTris = Simplifier.GetRemainingNumTris();

	NumExternalEdges = 0;
	for( int32 EdgeIndex = 0; EdgeIndex < ExternalEdges.Num(); EdgeIndex++ )
	{
		auto Edge = MakeTuple(
			GetPosition( Indexes[ EdgeIndex ] ),
			GetPosition( Indexes[ Cycle3( EdgeIndex ) ] )
		);
		int8* AdjCount = LockedEdges.Find( Edge );
		if( AdjCount )
		{
			ExternalEdges[ EdgeIndex ] = *AdjCount;
			NumExternalEdges++;
		}
	}

	float InvScale = 1.0f / PositionScale;
	for( uint32 i = 0; i < NumVerts; i++ )
	{
		GetPosition(i) *= InvScale;
		Bounds += GetPosition(i);
	}

	for( uint32 TriIndex = 0; TriIndex < NumTris; TriIndex++ )
	{
		// Remove UV mirroring bits
		MaterialIndexes[ TriIndex ] &= 0xffffff;
	}

	return FMath::Sqrt( MaxErrorSqr ) * InvScale;
}

void FCluster::Split( FGraphPartitioner& Partitioner, const FAdjacency& Adjacency ) const
{
	FDisjointSet DisjointSet( NumTris );
	for( int32 EdgeIndex = 0; EdgeIndex < Indexes.Num(); EdgeIndex++ )
	{
		Adjacency.ForAll( EdgeIndex,
			[ &DisjointSet ]( int32 EdgeIndex0, int32 EdgeIndex1 )
			{
				if( EdgeIndex0 > EdgeIndex1 )
					DisjointSet.UnionSequential( EdgeIndex0 / 3, EdgeIndex1 / 3 );
			} );
	}

	auto GetCenter = [ this ]( uint32 TriIndex )
	{
		FVector3f Center;
		Center  = GetPosition( Indexes[ TriIndex * 3 + 0 ] );
		Center += GetPosition( Indexes[ TriIndex * 3 + 1 ] );
		Center += GetPosition( Indexes[ TriIndex * 3 + 2 ] );
		return Center * (1.0f / 3.0f);
	};

	Partitioner.BuildLocalityLinks( DisjointSet, Bounds, MaterialIndexes, GetCenter );

	auto* RESTRICT Graph = Partitioner.NewGraph( NumTris * 3 );

	for( uint32 i = 0; i < NumTris; i++ )
	{
		Graph->AdjacencyOffset[i] = Graph->Adjacency.Num();

		uint32 TriIndex = Partitioner.Indexes[i];

		// Add shared edges
		for( int k = 0; k < 3; k++ )
		{
			Adjacency.ForAll( 3 * TriIndex + k,
				[ &Partitioner, Graph ]( int32 EdgeIndex, int32 AdjIndex )
				{
					Partitioner.AddAdjacency( Graph, AdjIndex / 3, 4 * 65 );
				} );
		}

		Partitioner.AddLocalityLinks( Graph, TriIndex, 1 );
	}
	Graph->AdjacencyOffset[ NumTris ] = Graph->Adjacency.Num();

	Partitioner.PartitionStrict( Graph, ClusterSize - 4, ClusterSize, false );
}

FAdjacency FCluster::BuildAdjacency() const
{
	FAdjacency Adjacency( Indexes.Num() );
	FEdgeHash EdgeHash( Indexes.Num() );

	for( int32 EdgeIndex = 0; EdgeIndex < Indexes.Num(); EdgeIndex++ )
	{
		Adjacency.Direct[ EdgeIndex ] = -1;

		EdgeHash.ForAllMatching( EdgeIndex, true,
			[ this ]( int32 CornerIndex )
			{
				return GetPosition( Indexes[ CornerIndex ] );
			},
			[&]( int32 EdgeIndex, int32 OtherEdgeIndex )
			{
				Adjacency.Link( EdgeIndex, OtherEdgeIndex );
			} );
	}

	return Adjacency;
}

uint32 FCluster::AddVert( const float* Vert, FHashTable& HashTable )
{
	const FVector3f& Position = *reinterpret_cast< const FVector3f* >( Vert );

	uint32 Hash = HashPosition( Position );
	uint32 NewIndex;
	for( NewIndex = HashTable.First( Hash ); HashTable.IsValid( NewIndex ); NewIndex = HashTable.Next( NewIndex ) )
	{
		if( 0 == FMemory::Memcmp( &GetPosition( NewIndex ), Vert, GetVertSize() * sizeof( float ) ) )
		{
			break;
		}
	}
	if( !HashTable.IsValid( NewIndex ) )
	{
		Verts.AddUninitialized( GetVertSize() );
		NewIndex = NumVerts++;
		HashTable.Add( Hash, NewIndex );

		FMemory::Memcpy( &GetPosition( NewIndex ), Vert, GetVertSize() * sizeof( float ) );
	}

	return NewIndex;
}



struct FNormalCone
{
	FVector3f	Axis;
	float	CosAngle;

	FNormalCone() {}
	FNormalCone( const FVector3f& InAxis )
		: Axis( InAxis )
		, CosAngle( 1.0f )
	{
		if( !Axis.Normalize() )
		{
			Axis = FVector3f( 0.0f, 0.0f, 1.0f );
		}
	}
};

FORCEINLINE FMatrix44f OrthonormalBasis( const FVector3f& Vec )
{
	float Sign = Vec.Z >= 0.0f ? 1.0f : -1.0f;
	float a = -1.0f / ( Sign + Vec.Z );
	float b = Vec.X * Vec.Y * a;
	
	return FMatrix44f(
		{ 1.0f + Sign * a * FMath::Square( Vec.X ), Sign * b, -Vec.X * Sign },
		{ b,     Sign + a * FMath::Square( Vec.Y ),           -Vec.Y },
		Vec,
		FVector3f::ZeroVector );
}

FMatrix44f CovarianceToBasis( const FMatrix44f& Covariance )
{
#if 0
	FMatrix44f Eigenvectors;
	FVector3f Eigenvalues;
	diagonalizeSymmetricMatrix( Covariance, Eigenvectors, Eigenvalues );

	//Eigenvectors = Eigenvectors.GetTransposed();

	uint32 i0 = Max3Index( Eigenvalues[0], Eigenvalues[1], Eigenvalues[2] );
	uint32 i1 = (1 << i0) & 3;
	uint32 i2 = (1 << i1) & 3;
	i1 = Eigenvalues[ i1 ] > Eigenvalues[ i2 ] ? i1 : i2;

	FVector3f Eigenvector0 = Eigenvectors.GetColumn( i0 );
	FVector3f Eigenvector1 = Eigenvectors.GetColumn( i1 );

	Eigenvector0.Normalize();
	Eigenvector1 -= ( Eigenvector0 | Eigenvector1 ) * Eigenvector1;
	Eigenvector1.Normalize();

	return FMatrix44f( Eigenvector0, Eigenvector1, Eigenvector0 ^ Eigenvector1, FVector3f::ZeroVector );
#else
	// Start with highest variance cardinal direction
	uint32 HighestVarianceDim = Max3Index( Covariance.M[0][0], Covariance.M[1][1], Covariance.M[2][2] );
	FVector3f Eigenvector0 = FMatrix44f::Identity.GetColumn( HighestVarianceDim );
	
	// Compute dominant eigenvector using power method
	for( int i = 0; i < 32; i++ )
	{
		Eigenvector0 = Covariance.TransformVector( Eigenvector0 );
		Eigenvector0.Normalize();
	}
	if( !Eigenvector0.IsNormalized() )
	{
		Eigenvector0 = FVector3f( 0.0f, 0.0f, 1.0f );
	}

	// Rotate matrix so that Z is Eigenvector0. This allows us to ignore Z dimension and turn this into a 2D problem.
	FMatrix44f ZSpace = OrthonormalBasis( Eigenvector0 );
	FMatrix44f ZLocalCovariance = Covariance * ZSpace;

	// Compute eigenvalues in XY plane. Solve for 2x2.
	float Det = ZLocalCovariance.M[0][0] * ZLocalCovariance.M[1][1] - ZLocalCovariance.M[0][1] * ZLocalCovariance.M[1][0];
	float Trace = ZLocalCovariance.M[0][0] + ZLocalCovariance.M[1][1];
	float Sqr = Trace * Trace - 4.0f * Det;
	if( Sqr < 0.0f )
	{
		return ZSpace;
	}
	float Sqrt = FMath::Sqrt( Sqr );
	
	float Eigenvalue1 = 0.5f * ( Trace + Sqrt );
	float Eigenvalue2 = 0.5f * ( Trace - Sqrt );

	float MinEigenvalue = FMath::Min( Eigenvalue1, Eigenvalue2 );
	float MaxEigenvalue = FMath::Max( Eigenvalue1, Eigenvalue2 );

	// Solve ( Eigenvalue * I - M ) * Eigenvector = 0
	FVector3f Eigenvector1;
	if( FMath::Abs( ZLocalCovariance.M[0][1] ) > FMath::Abs( ZLocalCovariance.M[1][0] ) )
	{
		Eigenvector1 = FVector3f( ZLocalCovariance.M[0][1], MaxEigenvalue - ZLocalCovariance.M[0][0], 0.0f );
	}
	else
	{
		Eigenvector1 = FVector3f( MaxEigenvalue - ZLocalCovariance.M[1][1], ZLocalCovariance.M[1][0], 0.0f );
	}

	Eigenvector1 = ZSpace.TransformVector( Eigenvector1 );
	//Eigenvector1 -= ( Eigenvector0 | Eigenvector1 ) * Eigenvector1;
	Eigenvector1.Normalize();

	return FMatrix44f( Eigenvector0, Eigenvector1, Eigenvector0 ^ Eigenvector1, FVector3f::ZeroVector );
#endif
}

void FCluster::Bound()
{
	Bounds = FBounds3f();
	SurfaceArea = 0.0f;
	
	TArray< FVector3f, TInlineAllocator<128> > Positions;
	Positions.SetNum( NumVerts, false );

	for( uint32 i = 0; i < NumVerts; i++ )
	{
		Positions[i] = GetPosition(i);
		Bounds += Positions[i];
	}
	SphereBounds = FSphere3f( Positions.GetData(), Positions.Num() );
	LODBounds = SphereBounds;
	
	float MaxEdgeLength2 = 0.0f;
	for( int i = 0; i < Indexes.Num(); i += 3 )
	{
		FVector3f v[3];
		v[0] = GetPosition( Indexes[ i + 0 ] );
		v[1] = GetPosition( Indexes[ i + 1 ] );
		v[2] = GetPosition( Indexes[ i + 2 ] );

		FVector3f Edge01 = v[1] - v[0];
		FVector3f Edge12 = v[2] - v[1];
		FVector3f Edge20 = v[0] - v[2];

		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge01.SizeSquared() );
		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge12.SizeSquared() );
		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge20.SizeSquared() );

		float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
		SurfaceArea += TriArea;
	}
	EdgeLength = FMath::Sqrt( MaxEdgeLength2 );
}

static void SanitizeFloat( float& X, float MinValue, float MaxValue, float DefaultValue )
{
	if( X >= MinValue && X <= MaxValue )
		;
	else if( X < MinValue )
		X = MinValue;
	else if( X > MaxValue )
		X = MaxValue;
	else
		X = DefaultValue;
}

void FCluster::SanitizeVertexData()
{
	const float FltThreshold = 1e12f;	// Fairly arbitrary threshold for sensible float values.
										// Should be large enough for all practical purposes, while still leaving enough headroom
										// so that overflows shouldn't be a concern.
										// With a 1e12 threshold, even x^3 fits comfortable in float range.

	for( uint32 VertexIndex = 0; VertexIndex < NumVerts; VertexIndex++ )
	{
		FVector3f& Position = GetPosition( VertexIndex );
		SanitizeFloat( Position.X, -FltThreshold, FltThreshold, 0.0f );
		SanitizeFloat( Position.Y, -FltThreshold, FltThreshold, 0.0f );
		SanitizeFloat( Position.Z, -FltThreshold, FltThreshold, 0.0f );

		FVector3f& Normal = GetNormal( VertexIndex );
		if( !(  Normal.X >= -FltThreshold && Normal.X <= FltThreshold &&
				Normal.Y >= -FltThreshold && Normal.Y <= FltThreshold &&
				Normal.Z >= -FltThreshold && Normal.Z <= FltThreshold ) )	// Don't flip condition. Intentionally written like this to be NaN-safe
		{
			Normal = FVector3f::UpVector;
		}	
		
		if( bHasColors )
		{
			FLinearColor& Color = GetColor( VertexIndex );
			SanitizeFloat( Color.R, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.G, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.B, 0.0f, 1.0f, 1.0f );
			SanitizeFloat( Color.A, 0.0f, 1.0f, 1.0f );
		}

		FVector2f* UVs = GetUVs( VertexIndex );
		for( uint32 UvIndex = 0; UvIndex < NumTexCoords; UvIndex++ )
		{
			SanitizeFloat( UVs[ UvIndex ].X, -FltThreshold, FltThreshold, 0.0f );
			SanitizeFloat( UVs[ UvIndex ].Y, -FltThreshold, FltThreshold, 0.0f );
		}
	}
}

FArchive& operator<<(FArchive& Ar, FMaterialRange& Range)
{
	Ar << Range.RangeStart;
	Ar << Range.RangeLength;
	Ar << Range.MaterialIndex;
	Ar << Range.BatchTriCounts;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FStripDesc& Desc)
{
	for (uint32 i = 0; i < 4; i++)
	{
		for (uint32 j = 0; j < 3; j++)
		{
			Ar << Desc.Bitmasks[i][j];
		}
	}
	Ar << Desc.NumPrevRefVerticesBeforeDwords;
	Ar << Desc.NumPrevNewVerticesBeforeDwords;
	return Ar;
}
} // namespace Nanite