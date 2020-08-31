// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "Bounds.h"

class FGraphPartitioner;

namespace Nanite
{

class FMeshlet
{
public:
	FMeshlet() {}
	FMeshlet(
		const TArray< FStaticMeshBuildVertex >& InVerts,
		const TArray< uint32 >& InIndexes,
		const TArray< int32 >& InMaterialIndexes,
		const TBitArray<>& InBoundaryEdges,
		uint32 TriBegin, uint32 TriEnd, const TArray< uint32 >& TriIndexes, uint32 NumTexCoords, bool bHasColors );

	FMeshlet( FMeshlet& SrcMeshlet, uint32 TriBegin, uint32 TriEnd, const TArray< uint32 >& TriIndexes );
	FMeshlet( const TArray< FMeshlet*, TInlineAllocator<16> >& MergeList );

	float	Simplify( uint32 NumTris );
	void	Split( FGraphPartitioner& Partitioner ) const;

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

	static const uint32	ClusterSize = 128;

	uint32		NumVerts = 0;
	uint32		NumTexCoords = 0;
	bool		bHasColors = false;

	TArray< float >		Verts;
	TArray< uint32 >	Indexes;
	TArray< int32 >		MaterialIndexes;
	TBitArray<>			BoundaryEdges;
	TBitArray<>			ExternalEdges;
	uint32				NumExternalEdges;

	TMap< uint32, uint32 >	AdjacentMeshlets;

	FBounds	Bounds;
	float	SurfaceArea = 0.0f;
	uint32	GUID = 0;
	int32	MipLevel = 0;
};

FORCEINLINE uint32 FMeshlet::GetVertSize() const
{
	return 6 + ( bHasColors ? 4 : 0 ) + NumTexCoords * 2;
}

FORCEINLINE FVector& FMeshlet::GetPosition( uint32 VertIndex )
{
	return *reinterpret_cast< FVector* >( &Verts[ VertIndex * GetVertSize() ] );
}

FORCEINLINE const FVector& FMeshlet::GetPosition( uint32 VertIndex ) const
{
	return *reinterpret_cast< const FVector* >( &Verts[ VertIndex * GetVertSize() ] );
}

FORCEINLINE float* FMeshlet::GetAttributes( uint32 VertIndex )
{
	return &Verts[ VertIndex * GetVertSize() + 3 ];
}

FORCEINLINE FVector& FMeshlet::GetNormal( uint32 VertIndex )
{
	return *reinterpret_cast< FVector* >( &Verts[ VertIndex * GetVertSize() + 3 ] );
}

FORCEINLINE const FVector& FMeshlet::GetNormal( uint32 VertIndex ) const
{
	return *reinterpret_cast< const FVector* >( &Verts[ VertIndex * GetVertSize() + 3 ] );
}

FORCEINLINE FLinearColor& FMeshlet::GetColor( uint32 VertIndex )
{
	return *reinterpret_cast< FLinearColor* >( &Verts[ VertIndex * GetVertSize() + 6 ] );
}

FORCEINLINE const FLinearColor& FMeshlet::GetColor( uint32 VertIndex ) const
{
	return *reinterpret_cast< const FLinearColor* >( &Verts[ VertIndex * GetVertSize() + 6 ] );
}

FORCEINLINE FVector2D* FMeshlet::GetUVs( uint32 VertIndex )
{
	return reinterpret_cast< FVector2D* >( &Verts[ VertIndex * GetVertSize() + 6 + ( bHasColors ? 4 : 0 ) ] );
}

FORCEINLINE const FVector2D* FMeshlet::GetUVs( uint32 VertIndex ) const
{
	return reinterpret_cast< const FVector2D* >( &Verts[ VertIndex * GetVertSize() + 6 + ( bHasColors ? 4 : 0 ) ] );
}

FTriCluster BuildCluster( const FMeshlet& Meshlet );

} // namespace Nanite