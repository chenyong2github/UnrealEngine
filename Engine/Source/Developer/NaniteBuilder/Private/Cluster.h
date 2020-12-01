// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "Bounds.h"

class FGraphPartitioner;

namespace Nanite
{

struct FMaterialRange
{
	uint32 RangeStart;
	uint32 RangeLength;
	uint32 MaterialIndex;

	friend FArchive& operator<<(FArchive& Ar, FMaterialRange& Range);
};

struct FStripDesc
{
	uint32 Bitmasks[4][3];
	uint32 NumPrevRefVerticesBeforeDwords;
	uint32 NumPrevNewVerticesBeforeDwords;

	friend FArchive& operator<<(FArchive& Ar, FStripDesc& Desc);
};

class FCluster
{
public:
	FCluster() {}
	FCluster(
		const TArray< FStaticMeshBuildVertex >& InVerts,
		const TArrayView< const uint32 >& InIndexes,
		const TArrayView< const int32 >& InMaterialIndexes,
		const TBitArray<>& InBoundaryEdges,
		uint32 TriBegin, uint32 TriEnd, const TArray< uint32 >& TriIndexes, uint32 NumTexCoords, bool bHasColors );

	FCluster( FCluster& SrcCluster, uint32 TriBegin, uint32 TriEnd, const TArray< uint32 >& TriIndexes );
	FCluster( const TArray< FCluster*, TInlineAllocator<16> >& MergeList );

	float	Simplify( uint32 NumTris );
	void	Split( FGraphPartitioner& Partitioner ) const;
	void	Bound();

private:
	void	FindExternalEdges();

public:
	uint32				GetVertSize() const;
	FVector&			GetPosition( uint32 VertIndex );
	float*				GetAttributes( uint32 VertIndex );
	FVector&			GetNormal( uint32 VertIndex );
	FLinearColor&		GetColor( uint32 VertIndex );
	FVector2D*			GetUVs( uint32 VertIndex );

	const FVector&		GetPosition( uint32 VertIndex ) const;
	const FVector&		GetNormal( uint32 VertIndex ) const;
	const FLinearColor&	GetColor( uint32 VertIndex ) const;
	const FVector2D*	GetUVs( uint32 VertIndex ) const;

	friend FArchive& operator<<(FArchive& Ar, FCluster& Cluster);

	static const uint32	ClusterSize = 128;

	uint32		NumVerts = 0;
	uint32		NumTris = 0;
	uint32		NumTexCoords = 0;
	bool		bHasColors = false;

	TArray< float >		Verts;
	TArray< uint32 >	Indexes;
	TArray< int32 >		MaterialIndexes;
	TBitArray<>			BoundaryEdges;
	TBitArray<>			ExternalEdges;
	uint32				NumExternalEdges;

	TMap< uint32, uint32 >	AdjacentClusters;

	FBounds		Bounds;
	float		SurfaceArea = 0.0f;
	uint32		GUID = 0;
	int32		MipLevel = 0;

	FUIntVector	QuantizedPosStart = { 0u, 0u, 0u };
	uint32		QuantizedPosShift;
	
	FVector		MeshBoundsMin;
	FVector		MeshBoundsDelta;

	float		EdgeLength = 0.0f;
	float		LODError = 0.0f;
	
	FSphere		SphereBounds;
	FSphere		LODBounds;

	uint32		GroupIndex			= MAX_uint32;
	uint32		GroupPartIndex		= MAX_uint32;
	uint32		GeneratingGroupIndex= MAX_uint32;

	TArray<FMaterialRange, TInlineAllocator<4>> MaterialRanges;
	TArray<FUIntVector>	QuantizedPositions;

	FStripDesc		StripDesc;
	TArray<uint8>	StripIndexData;
};

FORCEINLINE uint32 FCluster::GetVertSize() const
{
	return 6 + ( bHasColors ? 4 : 0 ) + NumTexCoords * 2;
}

FORCEINLINE FVector& FCluster::GetPosition( uint32 VertIndex )
{
	return *reinterpret_cast< FVector* >( &Verts[ VertIndex * GetVertSize() ] );
}

FORCEINLINE const FVector& FCluster::GetPosition( uint32 VertIndex ) const
{
	return *reinterpret_cast< const FVector* >( &Verts[ VertIndex * GetVertSize() ] );
}

FORCEINLINE float* FCluster::GetAttributes( uint32 VertIndex )
{
	return &Verts[ VertIndex * GetVertSize() + 3 ];
}

FORCEINLINE FVector& FCluster::GetNormal( uint32 VertIndex )
{
	return *reinterpret_cast< FVector* >( &Verts[ VertIndex * GetVertSize() + 3 ] );
}

FORCEINLINE const FVector& FCluster::GetNormal( uint32 VertIndex ) const
{
	return *reinterpret_cast< const FVector* >( &Verts[ VertIndex * GetVertSize() + 3 ] );
}

FORCEINLINE FLinearColor& FCluster::GetColor( uint32 VertIndex )
{
	return *reinterpret_cast< FLinearColor* >( &Verts[ VertIndex * GetVertSize() + 6 ] );
}

FORCEINLINE const FLinearColor& FCluster::GetColor( uint32 VertIndex ) const
{
	return *reinterpret_cast< const FLinearColor* >( &Verts[ VertIndex * GetVertSize() + 6 ] );
}

FORCEINLINE FVector2D* FCluster::GetUVs( uint32 VertIndex )
{
	return reinterpret_cast< FVector2D* >( &Verts[ VertIndex * GetVertSize() + 6 + ( bHasColors ? 4 : 0 ) ] );
}

FORCEINLINE const FVector2D* FCluster::GetUVs( uint32 VertIndex ) const
{
	return reinterpret_cast< const FVector2D* >( &Verts[ VertIndex * GetVertSize() + 6 + ( bHasColors ? 4 : 0 ) ] );
}

} // namespace Nanite