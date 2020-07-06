// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "Bounds.h"

namespace Nanite
{

template< uint32 NumTexCoords >
class TVert
{
	using VertType = TVert< NumTexCoords >;
	enum { NumUVs = NumTexCoords };
public:
	FVector			Position;
	FVector			Normal;
	FLinearColor	Color;
	FVector2D		UVs[ NumTexCoords ];

	FVector&		GetPos()					{ return Position; }
	const FVector&	GetPos() const				{ return Position; }
	float*			GetAttributes()				{ return (float*)&Normal; }
	const float*	GetAttributes() const		{ return (const float*)&Normal; }

	void Correct()
	{
		Normal.Normalize();
		Color = Color.GetClamped();
	}

	bool Equals( const VertType& a ) const
	{
		if( !PointsEqual(  Position,	a.Position ) ||
			!NormalsEqual( Normal,		a.Normal ) ||
			!Color.Equals( a.Color ) )
		{
			return false;
		}

		// UVs
		for( int32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
		{
			if( !UVsEqual( UVs[ UVIndex ], a.UVs[ UVIndex ] ) )
			{
				return false;
			}
		}

		return true;
	}

	bool operator==( const VertType& a ) const
	{
		if( Position		!= a.Position ||
			Normal			!= a.Normal ||
			Color			!= a.Color )
		{
			return false;
		}

		for( uint32 i = 0; i < NumTexCoords; i++ )
		{
			if( UVs[i] != a.UVs[i] )
			{
				return false;
			}
		}
		return true;
	}

	VertType operator+( const VertType& a ) const
	{
		VertType v;
		v.Position		= Position + a.Position;
		v.Normal		= Normal + a.Normal;
		v.Color			= Color + a.Color;

		for( uint32 i = 0; i < NumTexCoords; i++ )
		{
			v.UVs[i] = UVs[i] + a.UVs[i];
		}
		return v;
	}

	VertType operator-( const VertType& a ) const
	{
		VertType v;
		v.Position		= Position - a.Position;
		v.Normal		= Normal - a.Normal;
		v.Color			= Color - a.Color;
		
		for( uint32 i = 0; i < NumTexCoords; i++ )
		{
			v.UVs[i] = UVs[i] - a.UVs[i];
		}
		return v;
	}

	VertType operator*( const float a ) const
	{
		VertType v;
		v.Position		= Position * a;
		v.Normal		= Normal * a;
		v.Color			= Color * a;
		
		for( uint32 i = 0; i < NumTexCoords; i++ )
		{
			v.UVs[i] = UVs[i] * a;
		}
		return v;
	}

	VertType operator/( const float a ) const
	{
		float ia = 1.0f / a;
		return (*this) * ia;
	}
};
using VertType = TVert<2>;

class FMeshlet
{
public:
	FMeshlet() {}
	FMeshlet(
		const TArray< FStaticMeshBuildVertex >& InVerts,
		const TArray< uint32 >& InIndexes,
		const TArray< int32 >& InMaterialIndexes,
		const TBitArray<>& InBoundaryEdges,
		uint32 TriBegin, uint32 TriEnd, const TArray< uint32 >& TriIndexes );

	FMeshlet( FMeshlet& SrcMeshlet, uint32 TriBegin, uint32 TriEnd, const TArray< uint32 >& TriIndexes );
	FMeshlet( const TArray< FMeshlet*, TInlineAllocator<16> >& MergeList );

	float		Simplify( uint32 NumTris, float Scale, float* UVWeights );

private:
	void		FindExternalEdges();

public:
	static const uint32	ClusterSize = 128;

	TArray< VertType >	Verts;
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

FTriCluster BuildCluster( const FMeshlet& Meshlet );

} // namespace Nanite