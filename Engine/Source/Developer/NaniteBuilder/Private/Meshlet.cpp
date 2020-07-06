// Copyright Epic Games, Inc. All Rights Reserved.

#include "Meshlet.h"
#include "Bounds.h"
#include "MeshSimplify.h"
#include "GraphPartitioner.h"

template< typename T > FORCEINLINE uint32 Min3Index( const T A, const T B, const T C ) { return ( A < B ) ? ( ( A < C ) ? 0 : 2 ) : ( ( B < C ) ? 1 : 2 ); }
template< typename T > FORCEINLINE uint32 Max3Index( const T A, const T B, const T C ) { return ( A > B ) ? ( ( A > C ) ? 0 : 2 ) : ( ( B > C ) ? 1 : 2 ); }

namespace Nanite
{

FMeshlet::FMeshlet(
	const TArray< FStaticMeshBuildVertex >& InVerts,
	const TArray< uint32 >& InIndexes,
	const TArray< int32 >& InMaterialIndexes,
	const TBitArray<>& InBoundaryEdges,
	uint32 TriBegin, uint32 TriEnd, const TArray< uint32 >& TriIndexes )
{
	GUID = Murmur32( { TriBegin, TriEnd } );
	
	const uint32 NumTriangles = TriEnd - TriBegin;
	//ensure(NumTriangles <= FMeshlet::ClusterSize);

	Verts.Reserve( 128 );
	Indexes.Reserve( 3 * NumTriangles );
	BoundaryEdges.Reserve( 3 * NumTriangles );
	MaterialIndexes.Reserve( NumTriangles );

	check(InMaterialIndexes.Num() * 3 == InIndexes.Num());

	TMap< uint32, uint32 > OldToNewIndex;
	OldToNewIndex.Reserve( Verts.Max() );

	for( uint32 i = TriBegin; i < TriEnd; i++ )
	{
		uint32 TriIndex = TriIndexes[i];

		for( uint32 k = 0; k < 3; k++ )
		{
			uint32 OldIndex = InIndexes[ TriIndex * 3 + k ];
			uint32* NewIndexPtr = OldToNewIndex.Find( OldIndex );
			uint32 NewIndex = NewIndexPtr ? *NewIndexPtr : ~0u;

			if( NewIndex == ~0u )
			{
				NewIndex = Verts.AddUninitialized();
				OldToNewIndex.Add( OldIndex, NewIndex );
				
				const FStaticMeshBuildVertex& InVert = InVerts[ OldIndex ];

				VertType& NewVert = Verts.Last();

				NewVert.Position	= InVert.Position;
				NewVert.Normal		= InVert.TangentZ;
				NewVert.Color		= InVert.Color.ReinterpretAsLinear();

				NewVert.Normal		= NewVert.Normal.ContainsNaN() ? FVector::UpVector : NewVert.Normal;

				const uint32 NumTexCoords = sizeof( VertType::UVs ) / sizeof( FVector2D );
				for( uint32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
				{
					NewVert.UVs[ UVIndex ] = InVert.UVs[ UVIndex ].ContainsNaN() ? FVector2D::ZeroVector : InVert.UVs[ UVIndex ];
				}

				// Make sure this vertex is valid from the start
				NewVert.Correct();

				Bounds += NewVert.Position;
			}

			Indexes.Add( NewIndex );
			BoundaryEdges.Add( InBoundaryEdges[ TriIndex * 3 + k ] );
		}

		{
			const FVector& Position0 = InVerts[ InIndexes[ TriIndex * 3 + 0 ] ].Position;
			const FVector& Position1 = InVerts[ InIndexes[ TriIndex * 3 + 1 ] ].Position;
			const FVector& Position2 = InVerts[ InIndexes[ TriIndex * 3 + 2 ] ].Position;

			FVector Edge01 = Position1 - Position0;
			FVector Edge12 = Position2 - Position1;
			FVector Edge20 = Position0 - Position2;

			float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
			SurfaceArea += TriArea;
		}

		MaterialIndexes.Add( InMaterialIndexes[ TriIndex ] );
	}

	FindExternalEdges();

	//UE_LOG( LogStaticMesh, Log, TEXT("Leaf grid: %i, verts: %i, tris: %i"), GridLevel, Verts.Num(), Indexes.Num() / 3 );
}

// Split
FMeshlet::FMeshlet( FMeshlet& SrcMeshlet, uint32 TriBegin, uint32 TriEnd, const TArray< uint32 >& TriIndexes )
	: MipLevel( SrcMeshlet.MipLevel )
{
	GUID = Murmur32( { SrcMeshlet.GUID, TriBegin, TriEnd } );
	
	const uint32 NumTriangles = TriEnd - TriBegin;

	Verts.Reserve( 128 );
	Indexes.Reserve( 3 * NumTriangles );
	BoundaryEdges.Reserve( 3 * NumTriangles );
	MaterialIndexes.Reserve( NumTriangles );

	TMap< uint32, uint32 > OldToNewIndex;
	OldToNewIndex.Reserve( Verts.Max() );

	for( uint32 i = TriBegin; i < TriEnd; i++ )
	{
		uint32 TriIndex = TriIndexes[i];

		for( uint32 k = 0; k < 3; k++ )
		{
			uint32 OldIndex = SrcMeshlet.Indexes[ TriIndex * 3 + k ];
			uint32* NewIndexPtr = OldToNewIndex.Find( OldIndex );
			uint32 NewIndex = NewIndexPtr ? *NewIndexPtr : ~0u;

			if( NewIndex == ~0u )
			{
				NewIndex = Verts.Add( SrcMeshlet.Verts[ OldIndex ] );
				OldToNewIndex.Add( OldIndex, NewIndex );

				Bounds += Verts.Last().Position;
			}

			Indexes.Add( NewIndex );
			BoundaryEdges.Add( SrcMeshlet.BoundaryEdges[ TriIndex * 3 + k ] );
		}

		{
			const FVector& Position0 = SrcMeshlet.Verts[ SrcMeshlet.Indexes[ TriIndex * 3 + 0 ] ].Position;
			const FVector& Position1 = SrcMeshlet.Verts[ SrcMeshlet.Indexes[ TriIndex * 3 + 1 ] ].Position;
			const FVector& Position2 = SrcMeshlet.Verts[ SrcMeshlet.Indexes[ TriIndex * 3 + 2 ] ].Position;

			FVector Edge01 = Position1 - Position0;
			FVector Edge12 = Position2 - Position1;
			FVector Edge20 = Position0 - Position2;

			float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();
			SurfaceArea += TriArea;
		}

		const int32 MaterialIndex = SrcMeshlet.MaterialIndexes[ TriIndex ];
		MaterialIndexes.Add( MaterialIndex );
	}

	FindExternalEdges();

	//UE_LOG( LogStaticMesh, Log, TEXT("Split grid: %i, verts: %i, tris: %i"), GridLevel, Verts.Num(), Indexes.Num() / 3 );
}

// Merge
FMeshlet::FMeshlet( const TArray< FMeshlet*, TInlineAllocator<16> >& MergeList )
{
	uint32 NumOutputClusters = FMath::DivideAndRoundUp( MergeList.Num(), 2 );

	Verts.Reserve( ClusterSize * NumOutputClusters );
	Indexes.Reserve( 3 * ClusterSize * NumOutputClusters );
	BoundaryEdges.Reserve( 3 * ClusterSize * NumOutputClusters );
	MaterialIndexes.Reserve( ClusterSize * NumOutputClusters );

	TMultiMap< uint32, uint32 > HashTable;
	HashTable.Reserve( Verts.Max() );

	for( const FMeshlet* Child : MergeList )
	{
		Bounds += Child->Bounds;
		SurfaceArea	+= Child->SurfaceArea;

		// Can jump multiple levels but guarantee it steps at least 1.
		MipLevel = FMath::Max( MipLevel, Child->MipLevel + 1 );

		for( int32 i = 0; i < Child->Indexes.Num(); i++ )
		{
			const VertType& Vert = Child->Verts[ Child->Indexes[i] ];

			uint32 Index = ~0u;
			uint32 Hash = HashPosition( Vert.GetPos() );
			for( auto Iter = HashTable.CreateKeyIterator( Hash ); Iter; ++Iter )
			{
				if( Vert == Verts[ Iter.Value() ] )
				{
					Index = Iter.Value();
					break;
				}
			}
			if( Index == ~0u )
			{
				Index = Verts.Add( Vert );
				HashTable.Add( Hash, Index );
			}

			Indexes.Add( Index );
			BoundaryEdges.Add( Child->BoundaryEdges[i] );
		}

		for (int32 i = 0; i < Child->MaterialIndexes.Num(); i++)
		{
			const int32 MaterialIndex = Child->MaterialIndexes[i];
			MaterialIndexes.Add(MaterialIndex);
		}
	}

	//UE_LOG( LogStaticMesh, Log, TEXT("Merged verts: %i, tris: %i"), Verts.Num(), Indexes.Num() / 3 );
}

void CorrectAttributes( float* Attributes )
{
	FVector& Normal = *reinterpret_cast< FVector* >( Attributes );
	FLinearColor& Color = *reinterpret_cast< FLinearColor* >( Attributes + 3 );

	Normal.Normalize();
	Color = Color.GetClamped();
}

float FMeshlet::Simplify( uint32 TargetNumTris, float Scale, float* GlobalUVWeights )
{
	if( TargetNumTris * 3 >= (uint32)Indexes.Num() )
	{
		return 0.0f;
	}

	const uint32 NumTexCoords = sizeof( VertType::UVs ) / sizeof( FVector2D );
	const uint32 NumAttributes = ( sizeof( VertType ) - sizeof( FVector ) ) / sizeof(float);
	float AttributeWeights[ NumAttributes ] =
	{
		1.0f, 1.0f, 1.0f	// Normal
	};
	float* ColorWeights = AttributeWeights + 3;
	float* UVWeights = ColorWeights + 4;

	bool bHasColors = false;

	// Set weights if they are used
	if( bHasColors )
	{
		ColorWeights[0] = 0.0625f;
		ColorWeights[1] = 0.0625f;
		ColorWeights[2] = 0.0625f;
		ColorWeights[3] = 0.0625f;
	}

	for( int32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
	{
		UVWeights[ 2 * UVIndex + 0 ] = GlobalUVWeights[ UVIndex ];
		UVWeights[ 2 * UVIndex + 1 ] = GlobalUVWeights[ UVIndex ];
	}

#if 1
	float TriangleSize = FMath::Sqrt( SurfaceArea * 3.0f / Indexes.Num() );
	
	FFloat32 CurrentSize( FMath::Max( TriangleSize, THRESH_POINTS_ARE_SAME ) );
	FFloat32 DesiredSize( 0.25f );
	FFloat32 FloatScale( 1.0f );

	// Lossless scaling by only changing the float exponent.
	int32 Exponent = FMath::Clamp( (int)DesiredSize.Components.Exponent - (int)CurrentSize.Components.Exponent, -126, 127 );
	FloatScale.Components.Exponent = Exponent + 127;	//ExpBias
	// Scale ~= DesiredSize / CurrentSize
	Scale = FloatScale.FloatValue;
#endif

	for( auto& Vert : Verts )
	{
		Vert.Position *= Scale;

		for( int32 i = 0; i < NumTexCoords; i++ )
		{
			//Vert.UVs[i] *= Scale;
		}
	}

	FMeshSimplifier Simplifier( (float*)Verts.GetData(), Verts.Num(), Indexes.GetData(), Indexes.Num(), MaterialIndexes.GetData(), NumAttributes );

	Simplifier.SetBoundaryLocked( BoundaryEdges );
	
	Simplifier.SetAttributeWeights( AttributeWeights );
	Simplifier.SetCorrectAttributes( CorrectAttributes );
	Simplifier.SetEdgeWeight( 2.0f );

	float MaxErrorSqr = Simplifier.Simplify( Verts.Num(), TargetNumTris );

	check( Simplifier.GetRemainingNumVerts() > 0 );
	check( Simplifier.GetRemainingNumTris() > 0 );
	
	Simplifier.GetBoundaryUnlocked( BoundaryEdges );
	Simplifier.Compact();
	
	Verts.SetNum( Simplifier.GetRemainingNumVerts() );
	Indexes.SetNum( Simplifier.GetRemainingNumTris() * 3 );
	MaterialIndexes.SetNum( Simplifier.GetRemainingNumTris() );

	float InvScale = 1.0f / Scale;
	for( auto& Vert : Verts )
	{
		Vert.Position *= InvScale;

		for( int32 i = 0; i < NumTexCoords; i++ )
		{
			//Vert.UVs[i] *= InvScale;
		}
	}

	//UE_LOG( LogStaticMesh, Log, TEXT("Simplified verts: %i, tris: %i"), Verts.Num(), Indexes.Num() / 3 );

	return FMath::Sqrt( MaxErrorSqr ) * InvScale;
}

void FMeshlet::FindExternalEdges()
{
	ExternalEdges.Init( true, Indexes.Num() );
	NumExternalEdges = Indexes.Num();

	FHashTable HashTable( 1 << FMath::FloorLog2( Indexes.Num() ), Indexes.Num() );

	for( int32 EdgeIndex = 0; EdgeIndex < Indexes.Num(); EdgeIndex++ )
	{
		if( BoundaryEdges[ EdgeIndex ] )
		{
			ExternalEdges[ EdgeIndex ] = false;
			NumExternalEdges--;
			continue;
		}

		uint32 VertIndex0 = Indexes[ EdgeIndex ];
		uint32 VertIndex1 = Indexes[ Cycle3( EdgeIndex ) ];
	
		const FVector& Position0 = Verts[ VertIndex0 ].Position;
		const FVector& Position1 = Verts[ VertIndex1 ].Position;
	
		// Find edge with opposite direction that shares these 2 verts.
		/*
			  /\
			 /  \
			o-<<-o
			o->>-o
			 \  /
			  \/
		*/
		uint32 Hash0 = HashPosition( Position0 );
		uint32 Hash1 = HashPosition( Position1 );
		uint32 Hash = Murmur32( { Hash1, Hash0 } );

		uint32 OtherEdgeIndex;
		for( OtherEdgeIndex = HashTable.First( Hash ); HashTable.IsValid( OtherEdgeIndex ); OtherEdgeIndex = HashTable.Next( OtherEdgeIndex ) )
		{
			if( ExternalEdges[ OtherEdgeIndex ] )
			{
				uint32 OtherVertIndex0 = Indexes[ OtherEdgeIndex ];
				uint32 OtherVertIndex1 = Indexes[ Cycle3( OtherEdgeIndex ) ];
			
				if( Position0 == Verts[ OtherVertIndex1 ].Position &&
					Position1 == Verts[ OtherVertIndex0 ].Position )
				{
					// Found matching edge.
					ExternalEdges[ EdgeIndex ] = false;
					ExternalEdges[ OtherEdgeIndex ] = false;
					NumExternalEdges -= 2;
					break;
				}
			}
		}
		if( !HashTable.IsValid( OtherEdgeIndex ) )
		{
			HashTable.Add( Murmur32( { Hash0, Hash1 } ), EdgeIndex );
		}
	}
}




struct FNormalCone
{
	FVector	Axis;
	float	CosAngle;

	FNormalCone() {}
	FNormalCone( const FVector& InAxis )
		: Axis( InAxis )
		, CosAngle( 1.0f )
	{
		if( !Axis.Normalize() )
		{
			Axis = FVector( 0.0f, 0.0f, 1.0f );
		}
	}
};

FORCEINLINE FMatrix OrthonormalBasis( const FVector& Vec )
{
	float Sign = Vec.Z >= 0.0f ? 1.0f : -1.0f;
	float a = -1.0f / ( Sign + Vec.Z );
	float b = Vec.X * Vec.Y * a;
	
	return FMatrix(
		{ 1.0f + Sign * a * FMath::Square( Vec.X ), Sign * b, -Vec.X * Sign },
		{ b,     Sign + a * FMath::Square( Vec.Y ),           -Vec.Y },
		Vec,
		FVector::ZeroVector );
}

FMatrix CovarianceToBasis( const FMatrix& Covariance )
{
#if 0
	FMatrix Eigenvectors;
	FVector Eigenvalues;
	diagonalizeSymmetricMatrix( Covariance, Eigenvectors, Eigenvalues );

	//Eigenvectors = Eigenvectors.GetTransposed();

	uint32 i0 = Max3Index( Eigenvalues[0], Eigenvalues[1], Eigenvalues[2] );
	uint32 i1 = (1 << i0) & 3;
	uint32 i2 = (1 << i1) & 3;
	i1 = Eigenvalues[ i1 ] > Eigenvalues[ i2 ] ? i1 : i2;

	FVector Eigenvector0 = Eigenvectors.GetColumn( i0 );
	FVector Eigenvector1 = Eigenvectors.GetColumn( i1 );

	Eigenvector0.Normalize();
	Eigenvector1 -= ( Eigenvector0 | Eigenvector1 ) * Eigenvector1;
	Eigenvector1.Normalize();

	return FMatrix( Eigenvector0, Eigenvector1, Eigenvector0 ^ Eigenvector1, FVector::ZeroVector );
#else
	// Start with highest variance cardinal direction
	uint32 HighestVarianceDim = Max3Index( Covariance.M[0][0], Covariance.M[1][1], Covariance.M[2][2] );
	FVector Eigenvector0 = FMatrix::Identity.GetColumn( HighestVarianceDim );
	
	// Compute dominant eigenvector using power method
	for( int i = 0; i < 32; i++ )
	{
		Eigenvector0 = Covariance.TransformVector( Eigenvector0 );
		Eigenvector0.Normalize();
	}
	if( !Eigenvector0.IsNormalized() )
	{
		Eigenvector0 = FVector( 0.0f, 0.0f, 1.0f );
	}

	// Rotate matrix so that Z is Eigenvector0. This allows us to ignore Z dimension and turn this into a 2D problem.
	FMatrix ZSpace = OrthonormalBasis( Eigenvector0 );
	FMatrix ZLocalCovariance = Covariance * ZSpace;

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
	FVector Eigenvector1;
	if( FMath::Abs( ZLocalCovariance.M[0][1] ) > FMath::Abs( ZLocalCovariance.M[1][0] ) )
	{
		Eigenvector1 = FVector( ZLocalCovariance.M[0][1], MaxEigenvalue - ZLocalCovariance.M[0][0], 0.0f );
	}
	else
	{
		Eigenvector1 = FVector( MaxEigenvalue - ZLocalCovariance.M[1][1], ZLocalCovariance.M[1][0], 0.0f );
	}

	Eigenvector1 = ZSpace.TransformVector( Eigenvector1 );
	//Eigenvector1 -= ( Eigenvector0 | Eigenvector1 ) * Eigenvector1;
	Eigenvector1.Normalize();

	return FMatrix( Eigenvector0, Eigenvector1, Eigenvector0 ^ Eigenvector1, FVector::ZeroVector );
#endif
}


FTriCluster BuildCluster( const FMeshlet& Meshlet )
{
	FTriCluster Cluster;
	Cluster.NumVerts = Meshlet.Verts.Num();
	Cluster.NumTris  = Meshlet.Indexes.Num() / 3;
	Cluster.QuantizedPosShift = 0;	//TODO: seed this with something sensible like floor(log2(range)), so we can skip testing a lot of quantization levels
	Cluster.LODError = 0.0f;
	Cluster.BoxBounds[0] = Meshlet.Bounds.Min;
	Cluster.BoxBounds[1] = Meshlet.Bounds.Max;
	Cluster.GroupPartIndex = MAX_uint32;
	Cluster.GeneratingGroupIndex = MAX_uint32;
	
	TArray< FVector, TInlineAllocator<128> > Positions;
	Positions.SetNum( Cluster.NumVerts, false );

	for( int i = 0; i < Meshlet.Verts.Num(); i++ )
	{
		Positions[i] = Meshlet.Verts[i].Position;
	}
	Cluster.SphereBounds = FSphere( Positions.GetData(), Positions.Num() );
	Cluster.LODBounds = Cluster.SphereBounds;

	//auto& Normals = Positions;
	//Normals.Reset( Cluster.NumTris );

	FVector	SurfaceMean( 0.0f );
	float	SurfaceArea = 0.0f;
	
	float MaxEdgeLength2 = 0.0f;
	FVector AvgNormal = FVector::ZeroVector;
	for( int i = 0; i < Meshlet.Indexes.Num(); i += 3 )
	{
		FVector v[3];
		v[0] = Meshlet.Verts[ Meshlet.Indexes[ i + 0 ] ].Position;
		v[1] = Meshlet.Verts[ Meshlet.Indexes[ i + 1 ] ].Position;
		v[2] = Meshlet.Verts[ Meshlet.Indexes[ i + 2 ] ].Position;

		FVector Edge01 = v[1] - v[0];
		FVector Edge12 = v[2] - v[1];
		FVector Edge20 = v[0] - v[2];

		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge01.SizeSquared() );
		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge12.SizeSquared() );
		MaxEdgeLength2 = FMath::Max( MaxEdgeLength2, Edge20.SizeSquared() );

#if 0
		// Calculate normals
		FVector Normal = Edge01 ^ Edge20;
		if( Normal.Normalize( 1e-12 ) )
		{
			Normals.Add( Normal );
			AvgNormal += Normal;
		}
#endif

		float TriArea = 0.5f * ( Edge01 ^ Edge20 ).Size();

		for( int k = 0; k < 3; k++ )
			SurfaceMean += TriArea * v[k];

		SurfaceArea += TriArea;
	}
	Cluster.EdgeLength = FMath::Sqrt( MaxEdgeLength2 );
	SurfaceMean /= 3.0f * SurfaceArea;

#if 0
	// Minimal OBB using eigenvectors of covariance
	// https://www.geometrictools.com/Documentation/DynamicCollisionDetection.pdf
	float Covariance[6] = { 0 };
	for( int i = 0; i < Meshlet.Indexes.Num(); i += 3 )
	{
		FVector v[3];
		v[0] = Meshlet.Verts[ Meshlet.Indexes[ i + 0 ] ].Position;
		v[1] = Meshlet.Verts[ Meshlet.Indexes[ i + 1 ] ].Position;
		v[2] = Meshlet.Verts[ Meshlet.Indexes[ i + 2 ] ].Position;

		float TriArea = 0.5f * ( (v[2] - v[0]) ^ (v[1] - v[0]) ).Size();

		for( int k = 0; k < 3; k++ )
		{
			FVector Diff = v[k] - SurfaceMean;
			Covariance[0] += TriArea * Diff[0] * Diff[0];
			Covariance[1] += TriArea * Diff[0] * Diff[1];
			Covariance[2] += TriArea * Diff[0] * Diff[2];
			Covariance[3] += TriArea * Diff[1] * Diff[1];
			Covariance[4] += TriArea * Diff[1] * Diff[2];
			Covariance[5] += TriArea * Diff[2] * Diff[2];
		}
	}
	for( int j = 0; j < 6; j++ )
		Covariance[j] /= 12.0f * SurfaceArea;

	FMatrix Axis = CovarianceToBasis( FMatrix(
		{ Covariance[0], Covariance[1], Covariance[2] },
		{ Covariance[1], Covariance[3], Covariance[4] },
		{ Covariance[2], Covariance[4], Covariance[5] },
		FVector::ZeroVector ) );

	FMatrix InvAxis = Axis.GetTransposed();

	FBounds Bounds;
	for( int i = 0; i < Meshlet.Verts.Num(); i++ )
	{
		Bounds += InvAxis.TransformVector( Meshlet.Verts[i].Position );
	}

	FVector Center = 0.5f * ( Bounds[1] + Bounds[0] );
	FVector Extent = 0.5f * ( Bounds[1] - Bounds[0] );

	// Cluster space is [-1,1] cube.
	FMatrix BoundsToLocal = FScaleMatrix( Extent ) * FTranslationMatrix( Center ) * Axis;
#endif

#if 0
	FSphere NormalBounds;
	if( Normals.Num() )
	{
		NormalBounds = FSphere( Normals.GetData(), Normals.Num() );
	}

	FNormalCone SphereCone( NormalBounds.Center );
	FNormalCone AvgCone( AvgNormal );
	for( FVector& Normal : Normals )
	{
		SphereCone.CosAngle	= FMath::Min( Normal | SphereCone.Axis, SphereCone.CosAngle );
		AvgCone.CosAngle	= FMath::Min( Normal | AvgCone.Axis, AvgCone.CosAngle );
	}

	FNormalCone NormalCone;
	if( SphereCone.CosAngle > AvgCone.CosAngle )
	{
		NormalCone.Axis		= SphereCone.Axis;
		NormalCone.CosAngle	= SphereCone.CosAngle;
	}
	else
	{
		NormalCone.Axis		= AvgCone.Axis;
		NormalCone.CosAngle	= AvgCone.CosAngle;
	}

	if( NormalCone.CosAngle > 0.0f )
	{
		// Cone of plane normals is different from cone bounding their half spaces.
		// The half space's cone angle is the complement of the normal cone's angle and the axis is flipped.
		float SinAngle = FMath::Sqrt( 1.0f - NormalCone.CosAngle * NormalCone.CosAngle );

		Cluster.ConeAxis = -NormalCone.Axis;
		Cluster.ConeCosAngle = SinAngle;
		Cluster.ConeStart = FVector2D( -MAX_FLT, MAX_FLT );
		
		// Push half space cone outside of every triangle's half space.
		for( int i = 0; i < Meshlet.Indexes.Num(); i += 3 )
		{
			FVector v[3];
			v[0] = Meshlet.Verts[ Meshlet.Indexes[ i + 0 ] ].Position;
			v[1] = Meshlet.Verts[ Meshlet.Indexes[ i + 1 ] ].Position;
			v[2] = Meshlet.Verts[ Meshlet.Indexes[ i + 2 ] ].Position;

			FVector Normal = (v[2] - v[0]) ^ (v[1] - v[0]);
			if( Normal.Normalize( 1e-12 ) )
			{
				FPlane Plane( v[0] - Cluster.Bounds.Center, Normal );

				float CosAngle = Normal | NormalCone.Axis;
				float DistAlongAxis = Plane.W / CosAngle;

				Cluster.ConeStart.X = FMath::Max( Cluster.ConeStart.X, DistAlongAxis );
				Cluster.ConeStart.Y = FMath::Min( Cluster.ConeStart.Y, DistAlongAxis );
			}
		}
	}
	else
	{
		// No valid region to backface cull
		Cluster.ConeAxis = FVector( 0.0f, 0.0f, 1.0f );
		Cluster.ConeCosAngle = 2.0f;
		Cluster.ConeStart = FVector2D::ZeroVector;
	}
#endif

	return Cluster;
}

} // namespace Nanite