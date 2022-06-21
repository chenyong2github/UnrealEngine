// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMeshAlgo.h"

#if WITH_EDITOR

#include "NaniteDisplacedMesh.h"
#include "TessellationTable.h"
#include "Math/Bounds.h"

class FDisplacementMap
{
public:
	TArray64< uint8 >		SourceData;
	ETextureSourceFormat	SourceFormat;

	int32	BytesPerPixel;
	int32	SizeX;
	int32	SizeY;

	float	Magnitude;
	float	Center;

public:
	FDisplacementMap()
		: SourceFormat( TSF_G8 )
		, BytesPerPixel(1)
		, SizeX(1)
		, SizeY(1)
		, Magnitude( 0.0f )
		, Center( 0.0f )
	{
		SourceData.Add(0);
	}

	FDisplacementMap( FTextureSource& TextureSource, float InMagnitude, float InCenter )
		: Magnitude( InMagnitude )
		, Center( InCenter )
	{
		TextureSource.GetMipData( SourceData, 0 );

		SourceFormat  = TextureSource.GetFormat();
		BytesPerPixel = TextureSource.GetBytesPerPixel();
		
		SizeX = TextureSource.GetSizeX();
		SizeY = TextureSource.GetSizeY();
	}

	float Sample( FVector2f UV ) const
	{
		// Half texel
		UV.X = UV.X * SizeX - 0.5f;
		UV.Y = UV.Y * SizeY - 0.5f;

		int32 x0 = FMath::FloorToInt32( UV.X );
		int32 y0 = FMath::FloorToInt32( UV.Y );
		int32 x1 = x0 + 1;
		int32 y1 = y0 + 1;

		float wx1 = UV.X - x0;
		float wy1 = UV.Y - y0;
		float wx0 = 1.0f - wx1;
		float wy0 = 1.0f - wy1;

		return
			Sample( x0, y0 ) * wx0 * wy0 +
			Sample( x1, y0 ) * wx1 * wy0 +
			Sample( x0, y1 ) * wx0 * wy1 +
			Sample( x1, y1 ) * wx1 * wy1;
	}

	float Sample( int32 x, int32 y ) const
	{
		// UV wrap
		x = x % SizeX;
		y = y % SizeY;
		x += x < 0 ? SizeX : 0;
		y += y < 0 ? SizeY : 0;

		const uint8* PixelPtr = &SourceData[ int64( x + (int64)y * SizeX ) * BytesPerPixel ];

		float Displacement = 0.0f;
		if( SourceFormat == TSF_BGRA8 )
		{
			Displacement = float( PixelPtr[2] ) / 255.0f;
		}
		else if( SourceFormat == TSF_RGBA16 )
		{
			check(BytesPerPixel == sizeof(uint16) * 4);
			Displacement = float( *(uint16*)PixelPtr ) / 65535.0f;
		}
		else if( SourceFormat == TSF_RGBA16F )
		{
			FFloat16 HalfValue = *(FFloat16*)PixelPtr;
			Displacement = HalfValue;
		}
		else if( SourceFormat == TSF_G8 )
		{
			Displacement = float( PixelPtr[0] ) / 255.0f;
		}
		else if( SourceFormat == TSF_RGBA32F )
		{
			Displacement = *(float*)PixelPtr;
		}

		Displacement -= Center;
		Displacement *= Magnitude;

		return Displacement;
	}
};

// TODO This could be user provided
void DisplacementShader( FStaticMeshBuildVertex& Vertex, TArrayView< FDisplacementMap > DisplacementMaps )
{
	//float SideUVOffsetLength = 1.0f - FMath::Min( DisplaceLength, 1.0f );
	//float SideMask = FMath::Floor( DisplaceLength );

	int32 DisplacementIndex = FMath::FloorToInt( Vertex.UVs[1].X );
	float Displacement = 0.0f;
	if( DisplacementIndex < DisplacementMaps.Num() )
		Displacement = DisplacementMaps[ DisplacementIndex ].Sample( Vertex.UVs[0] );

	Vertex.TangentZ.Normalize();

	Vertex.Position += Vertex.TangentX * Displacement;
	Vertex.TangentX.Normalize();
}

FORCEINLINE uint32 HashPosition( const FVector3f& Position )
{
	union { float f; uint32 i; } x;
	union { float f; uint32 i; } y;
	union { float f; uint32 i; } z;

	x.f = Position.X;
	y.f = Position.Y;
	z.f = Position.Z;

	return Murmur32( {
		Position.X == 0.0f ? 0u : x.i,
		Position.Y == 0.0f ? 0u : y.i,
		Position.Z == 0.0f ? 0u : z.i
	} );
}

struct FLerpVert
{
	FVector3f Position;

	FVector3f TangentX;
	FVector3f TangentY;
	FVector3f TangentZ;

	FVector2f UVs[MAX_STATIC_TEXCOORDS];
	FLinearColor Color;

	FLerpVert() {}
	FLerpVert( FStaticMeshBuildVertex In )
		: Position( In.Position )
		, TangentX( In.TangentX )
		, TangentY( In.TangentY )
		, TangentZ( In.TangentZ )
	{
		for( uint32 i = 0; i < MAX_STATIC_TEXCOORDS; i++ )
			UVs[i] = In.UVs[i];

		Color = In.Color.ReinterpretAsLinear();
	}

	operator FStaticMeshBuildVertex() const
	{
		FStaticMeshBuildVertex Vert;
		Vert.Position = Position;
		Vert.TangentX = TangentX;
		Vert.TangentY = TangentY;
		Vert.TangentZ = TangentZ;
		Vert.Color    = Color.ToFColor( false );
		
		for( uint32 i = 0; i < MAX_STATIC_TEXCOORDS; i++ )
			Vert.UVs[i] = UVs[i];

		return Vert;
	}

	FLerpVert& operator+=( const FLerpVert& Other )
	{
		Position += Other.Position;
		TangentX += Other.TangentX;
		TangentY += Other.TangentY;
		TangentZ += Other.TangentZ;
		Color    += Other.Color;

		for( uint32 i = 0; i < MAX_STATIC_TEXCOORDS; i++ )
			UVs[i] += Other.UVs[i];

		return *this;
	}

	FLerpVert operator*( const float a ) const
	{
		FLerpVert Vert;
		Vert.Position = Position * a;
		Vert.TangentX = TangentX * a;
		Vert.TangentY = TangentY * a;
		Vert.TangentZ = TangentZ * a;
		Vert.Color    = Color * a;
		
		for( uint32 i = 0; i < MAX_STATIC_TEXCOORDS; i++ )
			Vert.UVs[i] = UVs[i] * a;

		return Vert;
	}
};

static void Tessellate(
	TArray< FStaticMeshBuildVertex >&	Verts,
	TArray< uint32 >&					Indexes,
	TArray< int32 >&					MaterialIndexes,
	float DiceRate )
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Tessellate);

	uint32 NumTris = Indexes.Num() / 3;

	Nanite::FTessellationTable& TessellationTable = Nanite::GetTessellationTable();
	
	TArray< uint32, TInlineAllocator<256> > NewVertIndexes;

	TMultiMap< uint32, int32 > HashTable;
	HashTable.Reserve( Verts.Num() * 2 );
	for( int i = 0; i < Verts.Num(); i++ )
	{
		HashTable.Add( HashPosition( Verts[i].Position ), i );
	}

	constexpr uint32 VertSize = sizeof( FStaticMeshBuildVertex ) / sizeof( float );
	
	/*
	===========
		v0
		/\
	e2 /  \ e0
	  /____\
	v2  e1  v1
	===========
	*/
	for( uint32 TriIndex = 0; TriIndex < NumTris; )
	{
		float TessFactors[3];
		for( int i = 0; i < 3; i++ )
		{
			const uint32 i0 = i;
			const uint32 i1 = (1 << i0) & 3;

			FVector3f p0 = Verts[ Indexes[ TriIndex * 3 + i0 ] ].Position;
			FVector3f p1 = Verts[ Indexes[ TriIndex * 3 + i1 ] ].Position;

			float EdgeLength = ( p0 - p1 ).Size();

			TessFactors[i] = FMath::Clamp( EdgeLength / DiceRate, 1, 0xffff );
		}

		uint32 EdgeIndex = FMath::Max3Index(
			TessFactors[0],
			TessFactors[1],
			TessFactors[2] );

		uint16 TessFactor = FMath::RoundToInt( TessFactors[ EdgeIndex ] );

		if( TessFactor > Nanite::FTessellationTable::MaxTessFactor )
		{
			// Split
			const uint32 e0 = EdgeIndex;
			const uint32 e1 = (1 << e0) & 3;
			const uint32 e2 = (1 << e1) & 3;

			const uint32 i0 = Indexes[ TriIndex * 3 + e0 ];
			const uint32 i1 = Indexes[ TriIndex * 3 + e1 ];
			const uint32 i2 = Indexes[ TriIndex * 3 + e2 ];

			// Deterministic weights
			uint32 Hash0 = HashPosition( Verts[ i0 ].Position );
			uint32 Hash1 = HashPosition( Verts[ i1 ].Position );

			// Diagsplit rules
			uint16 HalfSplit = TessFactor >> 1;
			uint16 TessFactor0 = Hash0 < Hash1 ? HalfSplit : TessFactor - HalfSplit;
			uint16 TessFactor1 = Hash0 < Hash1 ? TessFactor - HalfSplit : HalfSplit;

			float Weight0 = (float)TessFactor0 / TessFactor;
			float Weight1 = (float)TessFactor1 / TessFactor;

			FLerpVert NewVert;
			NewVert  = FLerpVert( Verts[ i0 ] ) * Weight0;
			NewVert += FLerpVert( Verts[ i1 ] ) * Weight1;

			uint32 Hash = HashPosition( NewVert.Position );
			uint32 NewIndex = ~0u;
			for( auto Iter = HashTable.CreateKeyIterator( Hash ); Iter; ++Iter )
			{
				if( 0 == FMemory::Memcmp( &Verts[ Iter.Value() ], &NewVert, VertSize * sizeof( float ) ) )
				{
					NewIndex = Iter.Value();
					break;
				}
			}
			if( NewIndex == ~0u )
			{
				NewIndex = Verts.Add( NewVert );
				HashTable.Add( Hash, NewIndex );
			}

			// Replace v0
			Indexes.Append( { NewIndex, i1, i2 } );

			int32 MaterialIndex = MaterialIndexes[ TriIndex ];
			MaterialIndexes.Add( MaterialIndex );
			NumTris++;

			// Replace v1
			Indexes[ TriIndex * 3 + e1 ] = NewIndex;
			continue;
		}

		uint32 e0 = FMath::Clamp< uint32 >( FMath::CeilToInt( TessFactors[0] ), 1, Nanite::FTessellationTable::MaxTessFactor ) - 1;
		uint32 e1 = FMath::Clamp< uint32 >( FMath::CeilToInt( TessFactors[1] ), 1, Nanite::FTessellationTable::MaxTessFactor ) - 1;
		uint32 e2 = FMath::Clamp< uint32 >( FMath::CeilToInt( TessFactors[2] ), 1, Nanite::FTessellationTable::MaxTessFactor ) - 1;
		
		uint32 i0 = Indexes[ TriIndex * 3 + 0 ];
		uint32 i1 = Indexes[ TriIndex * 3 + 1 ];
		uint32 i2 = Indexes[ TriIndex * 3 + 2 ];

		if( e0 + e1 + e2 == 0 )
		{
			TriIndex++;
			continue;
		}

		// Sorting can flip winding which we need to undo later.
		uint32 bFlipWinding = 0;
		if( e0 < e1 ) { Swap( e0, e1 );	Swap( i0, i2 ); bFlipWinding ^= 1; }
		if( e0 < e2 ) { Swap( e0, e2 );	Swap( i1, i2 ); bFlipWinding ^= 1; }
		if( e1 < e2 ) { Swap( e1, e2 );	Swap( i0, i1 ); bFlipWinding ^= 1; }

		uint32 Pattern = e0 + e1 * 16 + e2 * 256;

		FUintVector2 Offsets0 = TessellationTable.OffsetTable[ Pattern + 0 ];
		FUintVector2 Offsets1 = TessellationTable.OffsetTable[ Pattern + 1 ];

		NewVertIndexes.Reset();
		for( uint32 i = Offsets0.X; i < Offsets1.X; i++ )
		{
			uint32 Vert = TessellationTable.Verts[i];

			FVector3f Barycentrics;
			Barycentrics.X = Vert & 0xffff;
			Barycentrics.Y = Vert >> 16;
			Barycentrics.Z = Nanite::FTessellationTable::BarycentricMax - Barycentrics.X - Barycentrics.Y;
			Barycentrics  /= Nanite::FTessellationTable::BarycentricMax;

			FLerpVert NewVert;
			NewVert  = FLerpVert( Verts[ i0 ] ) * Barycentrics.X;
			NewVert += FLerpVert( Verts[ i1 ] ) * Barycentrics.Y;
			NewVert += FLerpVert( Verts[ i2 ] ) * Barycentrics.Z;

			uint32 Hash = HashPosition( NewVert.Position );
			uint32 NewIndex = ~0u;
			for( auto Iter = HashTable.CreateKeyIterator( Hash ); Iter; ++Iter )
			{
				if( 0 == FMemory::Memcmp( &Verts[ Iter.Value() ], &NewVert, VertSize * sizeof( float ) ) )
				{
					NewIndex = Iter.Value();
					break;
				}
			}
			if( NewIndex == ~0u )
			{
				NewIndex = Verts.Add( NewVert );
				HashTable.Add( Hash, NewIndex );
			}
			NewVertIndexes.Add( NewIndex );
		}

		for( uint32 i = Offsets0.Y; i < Offsets1.Y; i++ )
		{
			uint32 Triangle = TessellationTable.Indexes[i];

			uint32 VertIndexes[3];
			VertIndexes[0] = ( Triangle >>  0 ) & 1023;
			VertIndexes[1] = ( Triangle >> 10 ) & 1023;
			VertIndexes[2] = ( Triangle >> 20 ) & 1023;

			if( bFlipWinding )
				Swap( VertIndexes[1], VertIndexes[2] );

			Indexes.Append( {
				NewVertIndexes[ VertIndexes[0] ],
				NewVertIndexes[ VertIndexes[1] ],
				NewVertIndexes[ VertIndexes[2] ] } );

			int32 MaterialIndex = MaterialIndexes[ TriIndex ];
			MaterialIndexes.Add( MaterialIndex );
			NumTris++;
		}

		// Remove pre-diced triangle
		Indexes.RemoveAtSwap( TriIndex * 3 + 2, 1, false );
		Indexes.RemoveAtSwap( TriIndex * 3 + 1, 1, false );
		Indexes.RemoveAtSwap( TriIndex * 3 + 0, 1, false );
		MaterialIndexes.RemoveAtSwap( TriIndex, 1, false );
		NumTris--;
		TriIndex++;
	}
}

bool DisplaceNaniteMesh(
	const FNaniteDisplacedMeshParams& Parameters,
	const uint32 NumTextureCoord,
	TArray< FStaticMeshBuildVertex >& Verts,
	TArray< uint32 >& Indexes,
	TArray< int32 >& MaterialIndexes
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DisplaceNaniteMesh);

	// TODO: Make the mesh prepare and displacement logic extensible, and not hardcoded within this plugin

	// START - MESH PREPARE

	TArray<uint32> VertSamples;
	VertSamples.SetNumZeroed(Verts.Num());

	ParallelFor(TEXT("Nanite.Displace.Guide"), Verts.Num(), 1024,
	[&](int32 VertIndex)
	{
		FStaticMeshBuildVertex& TargetVert = Verts[VertIndex];

		TargetVert.TangentX = FVector3f::ZeroVector;

		for (int32 GuideVertIndex = 0; GuideVertIndex < Verts.Num(); ++GuideVertIndex)
		{
			const FStaticMeshBuildVertex& GuideVert = Verts[GuideVertIndex];

			if (GuideVert.UVs[1].Y >= 0.0f)
			{
				continue;
			}

			FVector3f GuideVertPos = GuideVert.Position;

			// Matches the geoscript prototype (TODO: Remove)
			const bool bApplyTolerance = true;
			if (bApplyTolerance)
			{
			    float Tolerance = 0.01f;
			    GuideVertPos /= Tolerance;
			    GuideVertPos.X = float(FMath::CeilToInt(GuideVertPos.X)) * Tolerance;
			    GuideVertPos.Y = float(FMath::CeilToInt(GuideVertPos.Y)) * Tolerance;
			    GuideVertPos.Z = float(FMath::CeilToInt(GuideVertPos.Z)) * Tolerance;
			}

			if (FVector3f::Distance(TargetVert.Position, GuideVertPos) < 0.1f)
			{
				++VertSamples[VertIndex];
				TargetVert.TangentX += GuideVert.TangentZ;
			}
		}

		if (VertSamples[VertIndex] > 0)
		{
			TargetVert.TangentX /= VertSamples[VertIndex];
			TargetVert.TangentX.Normalize();
		}
	});
	// END - MESH PREPARE

	Tessellate( Verts, Indexes, MaterialIndexes, Parameters.DiceRate );

	TArray< FDisplacementMap > DisplacementMaps;
	for( auto& DisplacementMap : Parameters.DisplacementMaps )
	{
		if( IsValid( DisplacementMap.Texture ) )
			DisplacementMaps.Add( FDisplacementMap( DisplacementMap.Texture->Source, DisplacementMap.Magnitude, DisplacementMap.Center ) );
		else
			DisplacementMaps.AddDefaulted();
	}

	ParallelFor( TEXT("Nanite.Displace.PF"), Verts.Num(), 1024,
		[&]( int32 VertIndex )
		{
			DisplacementShader( Verts[ VertIndex ], DisplacementMaps );
		} );

	return true;
}

#endif
