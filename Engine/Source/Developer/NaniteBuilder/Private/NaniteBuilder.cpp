// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteBuilder.h"
#include "Modules/ModuleManager.h"
#include "Components.h"
#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "Hash/CityHash.h"
#include "Math/UnrealMath.h"
#include "GraphPartitioner.h"
#include "Meshlet.h"
#include "MeshletDAG.h"
#include "MeshSimplify.h"
#include "DisjointSet.h"
#include "Async/ParallelFor.h"

// If static mesh derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.
#define NANITE_DERIVEDDATA_VER TEXT("5D4E1147-76DB-48D0-9953-27E15D01B945")


#define USE_IMPLICIT_TANGENT_SPACE		1	// must match define in ExportGBuffer.usf
#define CONSTRAINED_CLUSTER_CACHE_SIZE	32
#define MAX_CLUSTER_MATERIALS			64
#define USE_CONSTRAINED_CLUSTERS		1	// must match define in NaniteDataDecode.ush
											// Enable to constrain clusters to no more than 256 vertices and no index references outside of trailing window of CONSTRAINED_CLUSTER_CACHE_SIZE vertices.

#define INVALID_PART_INDEX				0xFFFFFFFFu
#define INVALID_GROUP_INDEX				0xFFFFFFFFu
#define INVALID_PAGE_INDEX				0xFFFFFFFFu
#define NUM_ROOT_PAGES					1u	// Should probably be made a per-resource option

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
		uint32& NumTexCoords,
		bool& bHasColors,
		const FMeshNaniteSettings& Settings) override;

	virtual bool Build(
		FResources& Resources,
		TArray< FStaticMeshBuildVertex>& Vertices,
		TArray< uint32 >& TriangleIndices,
		TArray< FStaticMeshSection, TInlineAllocator<1>>& Sections,
		uint32& NumTexCoords,
		bool& bHasColors,
		const FMeshNaniteSettings& Settings) override;
};

const FString& FBuilderModule::GetVersionString() const
{
	static FString VersionString;

	if (VersionString.IsEmpty())
	{
		VersionString = FString::Printf(TEXT("%s%s"), NANITE_DERIVEDDATA_VER, USE_CONSTRAINED_CLUSTERS ? TEXT("_CONSTRAINED") : TEXT(""));
	}

	return VersionString;
}

} // namespace Nanite

IMPLEMENT_MODULE( Nanite::FBuilderModule, NaniteBuilder );



namespace Nanite
{

FORCEINLINE uint32 GetTypeHash(const FUIntVector& V)
{
	return CityHash32( (const char*)&V, sizeof(FUIntVector) );
}

FORCEINLINE uint32 GetTypeHash( const FIntVector4& Vector )
{
	return CityHash32( (const char*)&Vector, sizeof( FIntVector4 ) );
}

/*
void UnpackBounds( FSphere& Bounds, const FSphere& LODBounds, const Nanite::FPackedBound& PackedBound )
{
	FFloat16 X, Y, Z, W;
	X.Encoded = PackedBound.XY & 0xFFFFu;
	Y.Encoded = PackedBound.XY >> 16;
	Z.Encoded = PackedBound.ZW & 0xFFFFu;
	W.Encoded = PackedBound.ZW >> 16;

	FVector4 BoundsDelta	= FVector4( X, Y, Z, W );
	FVector4 Value			= BoundsDelta + FVector4( LODBounds.Center, LODBounds.W );
	Bounds.Center			= FVector( Value );
	Bounds.W				= Value.W;
}

void PackBounds( Nanite::FPackedBound& PackedBound, const FSphere& Bounds, const FSphere& LODBounds )
{
	FVector4 BoundsDelta= FVector4( Bounds.Center, Bounds.W ) - FVector4( LODBounds.Center, LODBounds.W );
	PackedBound.XY		= FFloat16( BoundsDelta.X ).Encoded | ( FFloat16( BoundsDelta.Y ).Encoded << 16 );
	PackedBound.ZW		= FFloat16( BoundsDelta.Z ).Encoded | ( FFloat16( BoundsDelta.W ).Encoded << 16 );
}

void PackBoundsConservative( Nanite::FPackedBound& PackedBound, const FSphere& Bounds, const FSphere& LODBounds )
{
	PackBounds( PackedBound, Bounds, LODBounds );

	FSphere NewBounds;
	UnpackBounds( NewBounds, LODBounds, PackedBound );
	float NewRadius = ( ( Bounds.Center - NewBounds.Center ).Size() + Bounds.W - LODBounds.W );
	FFloat16 NewRadius16 = FFloat16( NewRadius );
	if( NewRadius16 < NewRadius )
		NewRadius16.Encoded += NewRadius16 < 0 ? -1 : 1;

	check( FMath::IsFinite(NewBounds.Center.X) );
	check( FMath::IsFinite(NewBounds.Center.Y) );
	check( FMath::IsFinite(NewBounds.Center.Z) );
	check( NewRadius16 >= NewRadius );
	PackedBound.ZW = ( NewRadius16.Encoded << 16) | ( PackedBound.ZW & 0xFFFFu );

	UnpackBounds( NewBounds, LODBounds, PackedBound );
	check( Bounds.IsInside( NewBounds, 1e-3f ) );
}
*/


template<uint32 BitLength>
class TFixedBitVector
{
	enum { QWordLength = (BitLength + 63) / 64 };
public:
	uint64 Data[QWordLength];

	void Clear()
	{
		FMemory::Memzero(Data);
	}

	void SetBit(uint32 Index)
	{
		check(Index < BitLength);
		Data[Index >> 6] |= 1ull << (Index & 63);
	}

	uint32 GetBit(uint32 Index)
	{
		check(Index < BitLength);
		return uint32(Data[Index >> 6] >> (Index & 63)) & 1u;
	}

	uint32 CountBits()
	{
		uint32 Result = 0;
		for (uint32 i = 0; i < QWordLength; i++)
		{
			Result += FGenericPlatformMath::CountBits(Data[i]);
		}
		return Result;
	}

	TFixedBitVector<BitLength> operator|(const TFixedBitVector<BitLength>& Other) const
	{
		TFixedBitVector<BitLength> Result;
		for (uint32 i = 0; i < QWordLength; i++)
		{
			Result.Data[i] = Data[i] | Other.Data[i];
		}
		return Result;
	}
};

// Naive bit writer for cooking purposes
class FBitWriter
{
public:
	FBitWriter(TArray<uint8>& Buffer) :
		Buffer(Buffer),
		PendingBits(0ull),
		NumPendingBits(0)
	{
	}

	void PutBits(uint32 Bits, uint32 NumBits)
	{
		check((uint64)Bits < (1ull << NumBits));
		PendingBits |= (uint64)Bits << NumPendingBits;
		NumPendingBits += NumBits;

		while (NumPendingBits >= 8)
		{
			Buffer.Add((uint8)PendingBits);
			PendingBits >>= 8;
			NumPendingBits -= 8;
		}
	}

	void Flush(uint32 Alignment=1)
	{
		if (NumPendingBits > 0)
			Buffer.Add((uint8)PendingBits);
		while (Buffer.Num() % Alignment != 0)
			Buffer.Add(0);
		PendingBits = 0;
		NumPendingBits = 0;
	}

private:
	TArray<uint8>& 	Buffer;
	uint64 			PendingBits;
	int32 			NumPendingBits;
};

struct FClusterGroupPart					// Whole group or a part of a group that has been split.
{
	TArray<uint32>	Clusters;				// Can be reordered during page allocation, so we need to store a list here.
	FSphere			Bounds;
	uint32			PageIndex;
	uint32			GroupIndex;				// Index of group this is a part of.
	uint32			HierarchyNodeIndex;
	uint32			HierarchyChildIndex;
	uint32			PageClusterOffset;
};

struct FPage
{
	uint32					PartsStartIndex = 0;
	uint32					PartsNum = 0;
	uint32					EncodedSize = 0;
	uint32					NumClusters = 0;
	TArray<uint8>			IndexData;
	TArray<uint8>			PositionData;
	TArray<uint8>			AttributeData;
};

FORCEINLINE bool IsRootPage(uint32 PageIndex)	// Keep in sync with ClusterCulling.usf
{
	return PageIndex < NUM_ROOT_PAGES;
}

FORCEINLINE void RemoveRootPagesFromRange(uint32& StartPage, uint32& NumPages)
{
	uint32_t NumRootPages = StartPage < NUM_ROOT_PAGES ? (NUM_ROOT_PAGES - StartPage) : 0u;
	if (NumRootPages > 0u)
	{
		NumRootPages = FMath::Min(NumRootPages, NumPages);
		StartPage += NumRootPages;
		NumPages -= NumRootPages;
	}

	if (NumPages == 0)
	{
		StartPage = 0;
	}
}

FORCEINLINE static FVector2D OctahedronEncode(FVector N)
{
	FVector AbsN = N.GetAbs();
	N /= (AbsN.X + AbsN.Y + AbsN.Z);

	if (N.Z < 0.0)
	{
		AbsN = N.GetAbs();
		N.X = (N.X >= 0.0f) ? (1.0f - AbsN.Y) : (AbsN.Y - 1.0f);
		N.Y = (N.Y >= 0.0f) ? (1.0f - AbsN.X) : (AbsN.X - 1.0f);
	}
	
	return FVector2D(N.X, N.Y);
}

FORCEINLINE static void OctahedronEncode(FVector N, int32& X, int32& Y, int32 QuantizationBits)
{
	const int32 QuantizationMaxValue = (1 << QuantizationBits) - 1;
	const float Scale = 0.5f * QuantizationMaxValue;
	const float Bias = 0.5f * QuantizationMaxValue + 0.5f;

	FVector2D Coord = OctahedronEncode(N);

	X = FMath::Clamp(int32(Coord.X * Scale + Bias), 0, QuantizationMaxValue);
	Y = FMath::Clamp(int32(Coord.Y * Scale + Bias), 0, QuantizationMaxValue);
}

FORCEINLINE static FVector OctahedronDecode(int32 X, int32 Y, int32 QuantizationBits)
{
	const int32 QuantizationMaxValue = (1 << QuantizationBits) - 1;
	float fx = X * (2.0f / QuantizationMaxValue) - 1.0f;
	float fy = Y * (2.0f / QuantizationMaxValue) - 1.0f;
	float fz = 1.0f - FMath::Abs(fx) - FMath::Abs(fy);
	float t = FMath::Clamp(-fz, 0.0f, 1.0f);
	fx += (fx >= 0.0f ? -t : t);
	fy += (fy >= 0.0f ? -t : t);

	return FVector(fx, fy, fz).GetUnsafeNormal();
}

FORCEINLINE static void OctahedronEncodePreciseSIMD( FVector N, int32& X, int32& Y, int32 QuantizationBits )
{
	const int32 QuantizationMaxValue = ( 1 << QuantizationBits ) - 1;
	FVector2D ScalarCoord = OctahedronEncode( N );

	const VectorRegister Scale = VectorSetFloat1( 0.5f * QuantizationMaxValue );
	const VectorRegister RcpScale = VectorSetFloat1( 2.0f / QuantizationMaxValue );
	VectorRegisterInt IntCoord = VectorFloatToInt( VectorMultiplyAdd( MakeVectorRegister( ScalarCoord.X, ScalarCoord.Y, ScalarCoord.X, ScalarCoord.Y ), Scale, Scale ) );	// x0, y0, x1, y1
	IntCoord = VectorIntAdd( IntCoord, MakeVectorRegisterInt( 0, 0, 1, 1 ) );
	VectorRegister Coord = VectorMultiplyAdd( VectorIntToFloat( IntCoord ), RcpScale, GlobalVectorConstants::FloatMinusOne );	// Coord = Coord * 2.0f / QuantizationMaxValue - 1.0f

	VectorRegister Nx = VectorSwizzle( Coord, 0, 2, 0, 2 );
	VectorRegister Ny = VectorSwizzle( Coord, 1, 1, 3, 3 );
	VectorRegister Nz = VectorSubtract( VectorSubtract( VectorOne(), VectorAbs( Nx ) ), VectorAbs( Ny ) );			// Nz = 1.0f - abs(Nx) - abs(Ny)

	VectorRegister T = VectorMin( Nz, VectorZero() );	// T = min(Nz, 0.0f)
	
	VectorRegister NxSign = VectorBitwiseAnd( Nx, GlobalVectorConstants::SignBit );
	VectorRegister NySign = VectorBitwiseAnd( Ny, GlobalVectorConstants::SignBit );

	Nx = VectorAdd(Nx, VectorBitwiseXor( T, NxSign ) );	// Nx += T ^ NxSign
	Ny = VectorAdd(Ny, VectorBitwiseXor( T, NySign ) );	// Ny += T ^ NySign
	
	VectorRegister RcpLen = VectorReciprocalSqrtAccurate( VectorMultiplyAdd( Nx, Nx, VectorMultiplyAdd( Ny, Ny, VectorMultiply( Nz, Nz ) ) ) );	// RcpLen = 1.0f / (Nx * Nx + Ny * Ny + Nz * Nz)
	VectorRegister Dots =	VectorMultiply(RcpLen, 
								VectorMultiplyAdd(Nx, VectorSetFloat1( N.X ), 
								VectorMultiplyAdd(Ny, VectorSetFloat1( N.Y ),
								VectorMultiply(Nz, VectorSetFloat1( N.Z ) ) ) ) );	// RcpLen * (Nx * N.x + Ny * N.y + Nz * N.z)
	VectorRegister Mask = MakeVectorRegister( 0xFFFFFFFCu, 0xFFFFFFFCu, 0xFFFFFFFCu, 0xFFFFFFFCu );
	VectorRegister LaneIndices = MakeVectorRegister( 0u, 1u, 2u, 3u );
	Dots = VectorBitwiseOr( VectorBitwiseAnd( Dots, Mask ), LaneIndices );
	
	// Calculate max component
	VectorRegister MaxDot = VectorMax( Dots, VectorSwizzle( Dots, 2, 3, 0, 1 ) );
	MaxDot = VectorMax( MaxDot, VectorSwizzle( MaxDot, 1, 2, 3, 0 ) );

	float fIndex = VectorGetComponent( MaxDot, 0 );
	uint32 Index = *(uint32*)&fIndex;
	
	uint32 IntCoordValues[ 4 ];
	VectorIntStore( IntCoord, IntCoordValues );
	X = FMath::Clamp((int32)(IntCoordValues[0] + ( Index & 1 )), 0, QuantizationMaxValue);
	Y = FMath::Clamp((int32)(IntCoordValues[1] + ( ( Index >> 1 ) & 1 )), 0, QuantizationMaxValue);
}

FORCEINLINE static void OctahedronEncodePrecise(FVector N, int32& X, int32& Y, int32 QuantizationBits)
{
	const int32 QuantizationMaxValue = (1 << QuantizationBits) - 1;
	FVector2D Coord = OctahedronEncode(N);

	const float Scale = 0.5f * QuantizationMaxValue;
	const float Bias = 0.5f * QuantizationMaxValue;
	int32 NX = FMath::Clamp(int32(Coord.X * Scale + Bias), 0, QuantizationMaxValue);
	int32 NY = FMath::Clamp(int32(Coord.Y * Scale + Bias), 0, QuantizationMaxValue);

	float MinError = 1.0f;
	int32 BestNX = 0;
	int32 BestNY = 0;
	for (int32 OffsetY = 0; OffsetY < 2; OffsetY++)
	{
		for (int32 OffsetX = 0; OffsetX < 2; OffsetX++)
		{
			int32 TX = NX + OffsetX;
			int32 TY = NY + OffsetY;
			if (TX <= QuantizationMaxValue && TY <= QuantizationMaxValue)
			{
				FVector RN = OctahedronDecode(TX, TY, QuantizationBits);
				float Error = FMath::Abs(1.0f - (RN | N));
				if (Error < MinError)
				{
					MinError = Error;
					BestNX = TX;
					BestNY = TY;
				}
			}
		}
	}

	X = BestNX;
	Y = BestNY;
}

FORCEINLINE static uint32 PackNormal(FVector Normal, uint32 QuantizationBits)
{
	int32 X, Y;
	OctahedronEncodePreciseSIMD(Normal, X, Y, QuantizationBits);

#if 0
	// Test against non-SIMD version
	int32 X2, Y2;
	OctahedronEncodePrecise(Normal, X2, Y2, QuantizationBits);
	FVector N0 = OctahedronDecode( X, Y, QuantizationBits );
	FVector N1 = OctahedronDecode( X2, Y2, QuantizationBits );
	float dt0 = Normal | N0;
	float dt1 = Normal | N1;
	check( dt0 >= dt1*0.99999f );
#endif
	
	return (Y << QuantizationBits) | X;
}

static uint32 PackMaterialTableRange(uint32 TriStart, uint32 TriLength, uint32 MaterialIndex)
{
	uint32 Packed = 0x00000000;
	// uint32 TriStart      :  8; // max 128 triangles
	// uint32 TriLength     :  8; // max 128 triangles
	// uint32 MaterialIndex :  6; // max  64 materials
	// uint32 Padding       : 10;
	check(TriStart <= 128);
	check(TriLength <= 128);
	check(MaterialIndex < 64);
	Packed |= TriStart;
	Packed |= TriLength << 8;
	Packed |= MaterialIndex << 16;
	return Packed;
}

static uint32 PackMaterialFastPath(uint32 Material0Length, uint32 Material0Index, uint32 Material1Length, uint32 Material1Index, uint32 Material2Index)
{
	uint32 Packed = 0x00000000;
	// Material Packed Range - Fast Path (32 bits)
	// uint Material0Length : 7;   // max 128 triangles (num minus one)
	// uint Material0Index  : 6;   // max  64 materials (0:Material0Length)
	// uint Material1Length : 7;   // max 128 triangles (num minus one)
	// uint Material1Index  : 6;   // max  64 materials (Material0Length:Material1Length)
	// uint Material2Index  : 6;   // max  64 materials (remainder)
	check(Material0Length >    1); // ensure minus one encoding is valid
	check(Material0Length <= 128);
	check(Material1Length <= 128);
	check(Material0Index  <   64);
	check(Material1Index  <   64);
	check(Material2Index  <   64);
	Material1Length = Material1Length > 0 ? Material1Length - 1 : 0;
	Packed |= (Material0Length   - 1);
	Packed |= (Material0Index  <<  7);
	Packed |= (Material1Length << 13);
	Packed |= (Material1Index  << 20);
	Packed |= (Material2Index  << 26);
	return Packed;
}

static uint32 PackMaterialSlowPath(uint32 MaterialTableOffset, uint32 MaterialTableLength)
{
	uint32 Packed = 0x00000000;
	// Material Packed Range - Slow Path (32 bits)
	// uint Padding         : 7;  // always 0 in slow path
	// uint BufferIndex     : 19; // 2^19 max value (tons, it's per prim)
	// uint BufferLength    : 6;  // max 127 ranges (num)
	check(MaterialTableOffset < 524288); // 2^19 - 1
	check(MaterialTableLength > 0); // clusters with 0 materials use fast path
	check(MaterialTableLength < 128);
	Packed |= (MaterialTableOffset  <<  7);
	Packed |= (MaterialTableLength << 26);
	return Packed;
}

static uint32 CalcMaterialTableSize( const Nanite::FTriCluster& InCluster )
{
	uint32 NumMaterials = InCluster.MaterialRanges.Num();
	return NumMaterials > 3 ? NumMaterials : 0;
}

static uint32 PackMaterialInfo(const Nanite::FTriCluster& InCluster, TArray<uint32>& OutMaterialTable, uint32 MaterialTableStartOffset)
{
	// Encode material ranges
	uint32 NumMaterialTriangles = 0;
	for (int32 RangeIndex = 0; RangeIndex < InCluster.MaterialRanges.Num(); ++RangeIndex)
	{
		check(InCluster.MaterialRanges[RangeIndex].RangeLength <= 128);
		check(InCluster.MaterialRanges[RangeIndex].RangeLength > 0);
		check(InCluster.MaterialRanges[RangeIndex].MaterialIndex < MAX_CLUSTER_MATERIALS);
		NumMaterialTriangles += InCluster.MaterialRanges[RangeIndex].RangeLength;
	}

	// All triangles accounted for in material ranges?
	check(NumMaterialTriangles == InCluster.NumTris);

	uint32 PackedMaterialInfo = 0x00000000;

	// The fast inline path can encode up to 3 materials
	if (InCluster.MaterialRanges.Num() <= 3)
	{
		uint32 Material0Length = 0;
		uint32 Material0Index = 0;
		uint32 Material1Length = 0;
		uint32 Material1Index = 0;
		uint32 Material2Index = 0;

		if (InCluster.MaterialRanges.Num() > 0)
		{
			const FMaterialRange& Material0 = InCluster.MaterialRanges[0];
			check(Material0.RangeStart == 0);
			Material0Length = Material0.RangeLength;
			Material0Index = Material0.MaterialIndex;
		}

		if (InCluster.MaterialRanges.Num() > 1)
		{
			const FMaterialRange& Material1 = InCluster.MaterialRanges[1];
			check(Material1.RangeStart == InCluster.MaterialRanges[0].RangeLength);
			Material1Length = Material1.RangeLength;
			Material1Index = Material1.MaterialIndex;
		}

		if (InCluster.MaterialRanges.Num() > 2)
		{
			const FMaterialRange& Material2 = InCluster.MaterialRanges[2];
			check(Material2.RangeStart == Material0Length + Material1Length);
			check(Material2.RangeLength == InCluster.NumTris - Material0Length - Material1Length);
			Material2Index = Material2.MaterialIndex;
		}

		PackedMaterialInfo = PackMaterialFastPath(Material0Length, Material0Index, Material1Length, Material1Index, Material2Index);
	}
	// Slow global table search path
	else
	{
		uint32 MaterialTableOffset = OutMaterialTable.Num() + MaterialTableStartOffset;
		uint32 MaterialTableLength = InCluster.MaterialRanges.Num();
		check(MaterialTableLength > 0);

		for (int32 RangeIndex = 0; RangeIndex < InCluster.MaterialRanges.Num(); ++RangeIndex)
		{
			const FMaterialRange& Material = InCluster.MaterialRanges[RangeIndex];
			OutMaterialTable.Add(PackMaterialTableRange(Material.RangeStart, Material.RangeLength, Material.MaterialIndex));
		}

		PackedMaterialInfo = PackMaterialSlowPath(MaterialTableOffset, MaterialTableLength);
	}

	return PackedMaterialInfo;
}

static void PackTriCluster(Nanite::FPackedTriCluster& OutCluster, const Nanite::FTriCluster& InCluster)
{
	// 0
	OutCluster.QuantizedPosStart	= InCluster.QuantizedPosStart;
	OutCluster.PositionOffset		= InCluster.PositionOffset;

	// 1
	OutCluster.MeshBoundsMin		= InCluster.MeshBoundsMin;
	OutCluster.IndexOffset			= InCluster.IndexOffset;

	// 2
	OutCluster.MeshBoundsDelta		= InCluster.MeshBoundsDelta;
	OutCluster.AttributeOffset		= InCluster.AttributeOffset;

	// 3
	check(InCluster.NumVerts < 512);
	check(InCluster.NumTris < 256);
	check(InCluster.BitsPerIndex < 16);
	check(InCluster.BitsPerAttrib < 128);
	check(InCluster.QuantizedPosShift < 64);
	OutCluster.NumVerts_NumTris_BitsPerIndex_QuantizedPosShift = InCluster.NumVerts | (InCluster.NumTris << (9)) | (InCluster.BitsPerIndex << (9 + 8)) | (InCluster.QuantizedPosShift << (9 + 8 + 4));
	OutCluster.BitsPerAttrib		= InCluster.BitsPerAttrib;
	OutCluster.GroupIndex			= InCluster.ClusterGroupIndex;
	OutCluster.Pad2					= 0;
	// 4
	OutCluster.LODBounds			= InCluster.LODBounds;

	// 5
	OutCluster.BoxBounds[0]			= InCluster.BoxBounds[0];
	// 6
	OutCluster.BoxBounds[1]			= InCluster.BoxBounds[1];
	//OutCluster.Bounds				= InCluster.SphereBounds;
	//OutCluster.BoundsXY				= InCluster.PackedBounds.XY;
	//OutCluster.BoundsZW				= InCluster.PackedBounds.ZW;

	// 7
	OutCluster.LODErrorAndEdgeLength	= FFloat16(InCluster.LODError).Encoded | (FFloat16(InCluster.EdgeLength).Encoded << 16);
	OutCluster.PackedMaterialInfo		= 0;	// Filled out by WritePages
	OutCluster.Flags					= NANITE_CLUSTER_FLAG_LEAF;
	OutCluster.Pad						= 0;
	
	for( int32 i = 0; i < InCluster.UVRanges.Num(); i++ )
	{
		OutCluster.UVRanges[i].Min			= InCluster.UVRanges[i].Min;
		OutCluster.UVRanges[i].Scale		= InCluster.UVRanges[i].Scale;

		OutCluster.UVRanges[i].GapStart[0]	= InCluster.UVRanges[i].GapStart[0];
		OutCluster.UVRanges[i].GapStart[1]	= InCluster.UVRanges[i].GapStart[1];
		OutCluster.UVRanges[i].GapLength[0]	= InCluster.UVRanges[i].GapLength[0];
		OutCluster.UVRanges[i].GapLength[1]	= InCluster.UVRanges[i].GapLength[1];
	}
}

static void PackHierarchyNode(Nanite::FPackedHierarchyNode& OutNode, const Nanite::FHierarchyNode& InNode, const TArray<FClusterGroup>& Groups, const TArray<FClusterGroupPart>& GroupParts)
{
	static_assert( MAX_RESOURCE_PAGES_BITS + MAX_CLUSTERS_PER_GROUP_BITS + MAX_GROUP_PARTS_BITS <= 32, "" );
	for (uint32 i = 0; i < 64; i++)
	{
		OutNode.LODBounds[i] = InNode.LODBounds[i];
		OutNode.Bounds[i] = InNode.Bounds[i];
		//OutNode.Misc[i].BoundsXY						= InNode.PackedBounds[i].XY;
		//OutNode.Misc[i].BoundsZW						= InNode.PackedBounds[i].ZW;

		check(InNode.NumChildren[i] <= MAX_CLUSTERS_PER_GROUP);
		OutNode.Misc[i].MinMaxLODError					= FFloat16( InNode.MinLODErrors[i] ).Encoded | ( FFloat16( InNode.MaxLODErrors[i] ).Encoded << 16 );
		OutNode.Misc[i].ChildStartReference				= InNode.ChildrenStartIndex[i];

		uint32 ResourcePageIndex_NumPages_GroupPartSize = 0;
		if( InNode.NumChildren[ i ] > 0 )
		{
			if( InNode.ClusterGroupPartIndex[ i ] != INVALID_PART_INDEX )
			{
				// Leaf node
				const FClusterGroup& Group = Groups[GroupParts[InNode.ClusterGroupPartIndex[i]].GroupIndex];
				uint32 GroupPartSize = InNode.NumChildren[ i ];

				// If group spans multiple pages, request all of them, except the root pages
				uint32 PageIndexStart = Group.PageIndexStart;
				uint32 PageIndexNum = Group.PageIndexNum;
				RemoveRootPagesFromRange(PageIndexStart, PageIndexNum);
				ResourcePageIndex_NumPages_GroupPartSize = (PageIndexStart << (MAX_CLUSTERS_PER_GROUP_BITS + MAX_GROUP_PARTS_BITS)) | (PageIndexNum << MAX_CLUSTERS_PER_GROUP_BITS) | GroupPartSize;
			}
			else
			{
				// Hierarchy node. No resource page or group size.
				ResourcePageIndex_NumPages_GroupPartSize = 0xFFFFFFFFu;
			}
		}
		OutNode.Misc[ i ].ResourcePageIndex_NumPages_GroupPartSize = ResourcePageIndex_NumPages_GroupPartSize;
	}
}

struct FMaterialTriangle
{
	uint32 Index0;
	uint32 Index1;
	uint32 Index2;
	uint32 MaterialIndex;
	uint32 RangeCount;
};

struct FCompareMaterialTriangleIndex
{
	// This groups the material ranges from largest to smallest, which is
	// more efficient for evaluating the sequences on the GPU, and also makes
	// the minus one encoding work (the first range must have more than 1 tri).
	FORCEINLINE bool operator()(const FMaterialTriangle& A, const FMaterialTriangle& B) const
	{
		if (A.RangeCount != B.RangeCount)
		{
			return (A.RangeCount > B.RangeCount);
		}

		return (A.MaterialIndex < B.MaterialIndex);
	}
};

static const uint32 POSITION_QUANTIZATION_BITS = 10;
static const uint32 NORMAL_QUANTIZATION_BITS = 12;
static const uint32 TANGENT_QUANTIZATION_BITS = 10;
static const uint32 TEXCOORD_QUANTIZATION_BITS = 10;
static const uint32 POSITION_QUANTIZATION_MAX_VALUE = (1u << POSITION_QUANTIZATION_BITS) - 1u;
static const uint32 TEXCOORD_QUANTIZATION_MAX_VALUE = (1u << TEXCOORD_QUANTIZATION_BITS) - 1u;

static_assert(POSITION_QUANTIZATION_BITS * 3 <= 32, "Doesn't fit in uint32");
static_assert(TANGENT_QUANTIZATION_BITS * 3 + 1 <= 32, "Doesn't fit in uint32");
static_assert(NORMAL_QUANTIZATION_BITS == 12, "Packing code assumes NORMAL_QUANTIZATION_BITS == 12");
static_assert(TEXCOORD_QUANTIZATION_BITS == 10, "Packing code assumes TEXCOORD_QUANTIZATION_BITS == 10");

static void CalculateQuantizedPositions(TArray< Nanite::FTriCluster >& Clusters, TArray< FMeshlet >& Meshlets, const FBounds& MeshBounds)
{
	// Quantize cluster positions to 10:10:10 cluster-local coordinates.
	const float FLOAT_UINT32_MAX = 4294967040.0f;	// Largest float value smaller than MAX_uint32: 1.11111111111111111111111b * 2^31
	const FVector ScaleToUINT = FVector(FLOAT_UINT32_MAX) / (MeshBounds.Max - MeshBounds.Min);
	const FVector BiasToUINT = -MeshBounds.Min * ScaleToUINT + 0.5f;

	auto QuantizeUInt = [](FUIntVector V, uint32 Shift)
	{
		//TODO: proper rounding
		V.X >>= Shift;
		V.Y >>= Shift;
		V.Z >>= Shift;
		return V;
	};

	const uint32 NumClusters = Clusters.Num();

	uint32 NumTotalTriangles = 0;
	uint32 NumTotalVertices = 0;
	for( const FTriCluster& Cluster : Clusters )
	{
		NumTotalTriangles += Cluster.NumTris;
		NumTotalVertices += Cluster.NumVerts;
	}


	// Quantize to UINT
	struct FUIntPosition
	{
		FUIntVector Position;
		uint32		ID;
	};

	TArray<FUIntPosition> UIntPositions;
	UIntPositions.AddUninitialized(NumTotalVertices);

	TArray<uint32> ClusterVertexOffsets;
	ClusterVertexOffsets.SetNumUninitialized( NumClusters );

	TArray<uint8> IDToShift;

	{
		FHashTable UIntPositionToID( 1u << FMath::CeilLogTwo( NumTotalVertices ), NumTotalVertices );

		uint32 NumIDs = 0;
		uint32 VertexOffset = 0;
		for( uint32 ClusterIndex = 0; ClusterIndex < NumClusters; ClusterIndex++ )
		{
			FMeshlet& Meshlet = Meshlets[ ClusterIndex ];

			FUIntVector UIntClusterMax = { 0, 0, 0 };
			FUIntVector UIntClusterMin = { MAX_uint32, MAX_uint32, MAX_uint32 };
			const uint32 NumVertices = Meshlet.Verts.Num();
			for( uint32 i = 0; i < NumVertices; i++ )
			{
				// Quantize to UINT
				FVector UnitPosition = ( Meshlet.Verts[ i ].Position - MeshBounds.Min ) / ( MeshBounds.Max - MeshBounds.Min );

				uint32 VertexIndex = VertexOffset + i;
				UIntPositions[ VertexIndex ].Position.X = (uint32)FMath::Clamp( (double)UnitPosition.X * (double)MAX_uint32, 0.0, (double)MAX_uint32 );
				UIntPositions[ VertexIndex ].Position.Y = (uint32)FMath::Clamp( (double)UnitPosition.Y * (double)MAX_uint32, 0.0, (double)MAX_uint32 );
				UIntPositions[ VertexIndex ].Position.Z = (uint32)FMath::Clamp( (double)UnitPosition.Z * (double)MAX_uint32, 0.0, (double)MAX_uint32 );

				UIntClusterMax.X = FMath::Max( UIntClusterMax.X, UIntPositions[ VertexIndex ].Position.X );
				UIntClusterMax.Y = FMath::Max( UIntClusterMax.Y, UIntPositions[ VertexIndex ].Position.Y );
				UIntClusterMax.Z = FMath::Max( UIntClusterMax.Z, UIntPositions[ VertexIndex ].Position.Z );
				UIntClusterMin.X = FMath::Min( UIntClusterMin.X, UIntPositions[ VertexIndex ].Position.X );
				UIntClusterMin.Y = FMath::Min( UIntClusterMin.Y, UIntPositions[ VertexIndex ].Position.Y );
				UIntClusterMin.Z = FMath::Min( UIntClusterMin.Z, UIntPositions[ VertexIndex ].Position.Z );

				// Map positions to IDs.
				// TODO: Simplifier should provide this information instead of us trying to infer it here.
				uint32 Hash = GetTypeHash( UIntPositions[ VertexIndex ].Position );
				bool bFound = false;
				for( uint32 j = UIntPositionToID.First( Hash ); UIntPositionToID.IsValid( j ); j = UIntPositionToID.Next( j ) )
				{
					if( UIntPositions[ j ].Position == UIntPositions[ VertexIndex ].Position )
					{
						UIntPositions[ VertexIndex ].ID = UIntPositions[ j ].ID;
						bFound = true;
						break;
					}
				}

				if( !bFound )
				{
					UIntPositions[ VertexIndex ].ID = NumIDs++;
					UIntPositionToID.Add( Hash, VertexIndex );
				}
			}

			uint32 ClusterShift = 0;
			while(true)
			{
				FUIntVector UIntMin = QuantizeUInt( UIntClusterMin, ClusterShift );
				FUIntVector UIntMax = QuantizeUInt( UIntClusterMax, ClusterShift );
				if( UIntMax.X - UIntMin.X <= POSITION_QUANTIZATION_MAX_VALUE ||
					UIntMax.Y - UIntMin.Y <= POSITION_QUANTIZATION_MAX_VALUE ||
					UIntMax.Z - UIntMin.Z <= POSITION_QUANTIZATION_MAX_VALUE )
				{
					break;
				}
				ClusterShift++;
			}
			Clusters[ClusterIndex].QuantizedPosShift = ClusterShift;

			ClusterVertexOffsets[ ClusterIndex ] = VertexOffset;
			VertexOffset += NumVertices;
		}
		IDToShift.SetNumZeroed( NumIDs );
	}

	bool QuantizationShiftsChanged = true;
	while (QuantizationShiftsChanged)	// Keep going until no cluster had to expand its quantization level
	{
		QuantizationShiftsChanged = false;
		for( uint32 ClusterIndex = 0; ClusterIndex < NumClusters; ClusterIndex++ )
		{
			FTriCluster& Cluster = Clusters[ ClusterIndex ];
			const FMeshlet& Meshlet = Meshlets[ ClusterIndex ];

			const uint32 ClusterVertexOffset = ClusterVertexOffsets[ ClusterIndex ];
			const uint32 NumClusterVerts = Cluster.NumVerts;

			FUIntVector UIntClusterMax;
			FUIntVector UIntClusterMin;
			uint32 ClusterShift = Cluster.QuantizedPosShift;
			while (true)
			{
				// Calculate bounds at current quantization level
				UIntClusterMax = { 0, 0, 0 };
				UIntClusterMin = { MAX_uint32, MAX_uint32, MAX_uint32 };
				for (uint32 i = 0; i < NumClusterVerts; i++)
				{
					FVector VertPosition = Meshlet.Verts[i].Position;
					const FUIntVector& UIntPosition = UIntPositions[ClusterVertexOffset + i].Position;
					uint32 ID = UIntPositions[ClusterVertexOffset + i].ID;

					// Quantization level of vertex is max of all clusters it is a part of
					uint8& PositionShift = IDToShift[ID];
					PositionShift = FMath::Max(PositionShift, (uint8)ClusterShift);

					// Extend bounds with vertex
					FUIntVector QuantizedPosition = QuantizeUInt(UIntPosition, PositionShift);
					check(PositionShift >= ClusterShift);
					QuantizedPosition.X <<= (PositionShift - ClusterShift);
					QuantizedPosition.Y <<= (PositionShift - ClusterShift);
					QuantizedPosition.Z <<= (PositionShift - ClusterShift);
					UIntClusterMax.X = FMath::Max(UIntClusterMax.X, QuantizedPosition.X);
					UIntClusterMax.Y = FMath::Max(UIntClusterMax.Y, QuantizedPosition.Y);
					UIntClusterMax.Z = FMath::Max(UIntClusterMax.Z, QuantizedPosition.Z);
					UIntClusterMin.X = FMath::Min(UIntClusterMin.X, QuantizedPosition.X);
					UIntClusterMin.Y = FMath::Min(UIntClusterMin.Y, QuantizedPosition.Y);
					UIntClusterMin.Z = FMath::Min(UIntClusterMin.Z, QuantizedPosition.Z);
				}

				uint32 Delta = FMath::Max3(UIntClusterMax.X - UIntClusterMin.X, UIntClusterMax.Y - UIntClusterMin.Y, UIntClusterMax.Z - UIntClusterMin.Z);
				if (Delta <= POSITION_QUANTIZATION_MAX_VALUE)
					break;	// If it doesn't fit try again at next quantization level
				ClusterShift++;
				QuantizationShiftsChanged = true;
			}

			Cluster.QuantizedPosStart = UIntClusterMin;
			Cluster.QuantizedPosShift = ClusterShift;
		}
	}

	FVector MeshBoundsScale = (MeshBounds.Max - MeshBounds.Min) / FLOAT_UINT32_MAX;

	ParallelFor( NumClusters,
		[&]( uint32 ClusterIndex )
		{
			FTriCluster& Cluster = Clusters[ClusterIndex];
			const FMeshlet& Meshlet = Meshlets[ClusterIndex];
			
			// Store MeshBounds in every cluster. Would it be better to store it in some sort of per instance data?
			Cluster.MeshBoundsMin = MeshBounds.Min;
			Cluster.MeshBoundsDelta = MeshBoundsScale * (1u << Cluster.QuantizedPosShift);	// As long as we store the mesh bounds per instance, we might as well losslessly scale it by the quantization

			const uint32 NumClusterVerts = Cluster.NumVerts;
			const FUIntVector QuantizedPosStart = Cluster.QuantizedPosStart;
			const uint32 ClusterShift = Cluster.QuantizedPosShift;

			const uint32 ClusterVertexOffset = ClusterVertexOffsets[ClusterIndex];
			Cluster.QuantizedPositions.SetNumUninitialized(NumClusterVerts);
			check(Cluster.NumVerts == Cluster.QuantizedPositions.Num());
		
			for (uint32 VertexIndex = 0; VertexIndex < NumClusterVerts; VertexIndex++)
			{
				const FUIntPosition& UIntPosition = UIntPositions[ClusterVertexOffset + VertexIndex];
				uint32 PositionShift = IDToShift[UIntPosition.ID];
				check(PositionShift >= ClusterShift);

				FUIntVector QuantizedPosition = QuantizeUInt(UIntPosition.Position, PositionShift);
				QuantizedPosition.X <<= (PositionShift - ClusterShift);
				QuantizedPosition.Y <<= (PositionShift - ClusterShift);
				QuantizedPosition.Z <<= (PositionShift - ClusterShift);
				check(QuantizedPosition.X >= QuantizedPosStart.X && QuantizedPosition.Y >= QuantizedPosStart.Y && QuantizedPosition.Z >= QuantizedPosStart.Z);

				QuantizedPosition.X -= QuantizedPosStart.X;
				QuantizedPosition.Y -= QuantizedPosStart.Y;
				QuantizedPosition.Z -= QuantizedPosStart.Z;
				check(QuantizedPosition.X <= POSITION_QUANTIZATION_MAX_VALUE && QuantizedPosition.Y <= POSITION_QUANTIZATION_MAX_VALUE && QuantizedPosition.Z <= POSITION_QUANTIZATION_MAX_VALUE);
				Cluster.QuantizedPositions[VertexIndex] = QuantizedPosition;
			}
		} );
}

static void EncodeGeometryData(	Nanite::FTriCluster& Cluster, const Nanite::FMeshlet& Meshlet, uint32 NumTexCoords,
								TArray<uint8>& IndexData, TArray<uint8>& PositionData, TArray<uint8>& AttributeData)
{
	const uint32 NumClusterVerts = Cluster.NumVerts;
	const uint32 NumClusterTris = Cluster.NumTris;

	const FUIntVector QuantizedPosStart = Cluster.QuantizedPosStart;
	const uint32 ClusterShift = Cluster.QuantizedPosShift;

	check(NumClusterTris <= 128);
	
	if (NumTexCoords > 2) NumTexCoords = 2;	//TODO: hack
	
	FBitWriter BitWriter_Index(IndexData);
	FBitWriter BitWriter_Position(PositionData);
	FBitWriter BitWriter_Attribute(AttributeData);


	// Write triangles indices. Indices are stored in a dense packed bitstream using ceil(log2(NumClusterVerices)) bits per index. The shaders implement unaligned bitstream reads to support this.

	const uint32 BitsPerIndex = NumClusterVerts > 1 ? (FGenericPlatformMath::FloorLog2(NumClusterVerts - 1) + 1) : 0;
	
	// Write triangle indices
	for (uint32 TriIndex = 0; TriIndex < NumClusterTris; ++TriIndex)
	{
		uint32 Index0 = Meshlet.Indexes[TriIndex * 3 + 0];
		uint32 Index1 = Meshlet.Indexes[TriIndex * 3 + 1];
		uint32 Index2 = Meshlet.Indexes[TriIndex * 3 + 2];
		uint32 PackedIndices = (Index2 << (BitsPerIndex * 2)) | (Index1 << BitsPerIndex) | Index0;
		BitWriter_Index.PutBits(PackedIndices, BitsPerIndex * 3);
	}
	BitWriter_Index.Flush(sizeof(uint32));

	Cluster.BitsPerIndex = BitsPerIndex;

	check(NumClusterVerts > 0);

	// Generate quantized texture coordinates
	TArray<uint32> PackedUVs;
	PackedUVs.SetNumUninitialized( NumClusterVerts * NumTexCoords );
	Cluster.UVRanges.SetNumUninitialized( NumTexCoords );

	for( uint32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
	{
		// Block compress texture coordinates
		// Texture coordinates are stored relative to the clusters min/max UV coordinates.
		// UV seams result in very large sparse bounding rectangles. To mitigate this the largest gap in U and V of the bounding rectangle are excluded from the coding space.
		// Decoding this is very simple: UV += (UV >= GapStart) ? GapRange : 0;

		// Generate sorted U and V arrays.
		TArray<float> UValues;
		TArray<float> VValues;
		UValues.AddUninitialized(NumClusterVerts);
		VValues.AddUninitialized(NumClusterVerts);
		for (uint32 i = 0; i < NumClusterVerts; i++)
		{
			const FVector2D& UV = Meshlet.Verts[ i ].UVs[ UVIndex ];
			UValues[i] = UV.X;
			VValues[i] = UV.Y;
		}

		UValues.Sort();
		VValues.Sort();

		// Find largest gap between sorted UVs
		FVector2D LargestGapStart = FVector2D(UValues[0], VValues[0]);
		FVector2D LargestGapEnd = FVector2D(UValues[0], VValues[0]);
		for (uint32 i = 0; i < NumClusterVerts - 1; i++)
		{
			if (UValues[i + 1] - UValues[i] > LargestGapEnd.X - LargestGapStart.X)
			{
				LargestGapStart.X = UValues[i];
				LargestGapEnd.X = UValues[i + 1];
			}
			if (VValues[i + 1] - VValues[i] > LargestGapEnd.Y - LargestGapStart.Y)
			{
				LargestGapStart.Y = VValues[i];
				LargestGapEnd.Y = VValues[i + 1];
			}
		}

		const FVector2D UVMin = FVector2D(UValues[0], VValues[0]);
		const FVector2D UVMax = FVector2D(UValues[NumClusterVerts - 1], VValues[NumClusterVerts - 1]);
		const FVector2D UVDelta = FVector2D::Max(UVMax - UVMin, FVector2D(1e-7f, 1e-7f));
		const FVector2D UVRcpDelta = FVector2D(1.0f, 1.0f) / UVDelta;

		const FVector2D NormalizedGapStart = (LargestGapStart - UVMin) * UVRcpDelta;
		const FVector2D NormalizedGapEnd = (LargestGapEnd - UVMin) * UVRcpDelta;

		const FVector2D NormalizedNonGap = FVector2D(1.0f, 1.0f) - (NormalizedGapEnd - NormalizedGapStart);

		const uint32 NU = (uint32)FMath::Clamp((TEXCOORD_QUANTIZATION_MAX_VALUE - 2) / NormalizedNonGap.X, (float)TEXCOORD_QUANTIZATION_MAX_VALUE, (float)0xFFFF);
		const uint32 NV = (uint32)FMath::Clamp((TEXCOORD_QUANTIZATION_MAX_VALUE - 2) / NormalizedNonGap.Y, (float)TEXCOORD_QUANTIZATION_MAX_VALUE, (float)0xFFFF);

		int32 GapStartU = TEXCOORD_QUANTIZATION_MAX_VALUE + 1;
		int32 GapStartV = TEXCOORD_QUANTIZATION_MAX_VALUE + 1;
		int32 GapLengthU = 0;
		int32 GapLengthV = 0;
		if (NU > TEXCOORD_QUANTIZATION_MAX_VALUE)
		{
			GapStartU = int32(NormalizedGapStart.X * NU + 0.5f) + 1;
			const int32 GapEndU = int32(NormalizedGapEnd.X * NU + 0.5f);
			GapLengthU = FMath::Max(GapEndU - GapStartU, 0);
		}
		if (NV > TEXCOORD_QUANTIZATION_MAX_VALUE)
		{
			GapStartV = int32(NormalizedGapStart.Y * NV + 0.5f) + 1;
			const int32 GapEndV = int32(NormalizedGapEnd.Y * NV + 0.5f);
			GapLengthV = FMath::Max(GapEndV - GapStartV, 0);
		}

		for (uint32 i = 0; i < NumClusterVerts; i++)
		{
			const FVector2D& UV = Meshlet.Verts[ i ].UVs[ UVIndex ];
			const FVector2D NormalizedUV = ((UV - UVMin) * UVRcpDelta).ClampAxes(0.0f, 1.0f);

			int32 U = int32(NormalizedUV.X * NU + 0.5f);
			int32 V = int32(NormalizedUV.Y * NV + 0.5f);
			if (U >= GapStartU) U -= GapLengthU; 
			if (V >= GapStartV) V -= GapLengthV;
			check(U >= 0 && U <= TEXCOORD_QUANTIZATION_MAX_VALUE);
			check(V >= 0 && V <= TEXCOORD_QUANTIZATION_MAX_VALUE);
			PackedUVs[ NumClusterVerts * UVIndex + i ] = (V << TEXCOORD_QUANTIZATION_BITS) | U;
		}

		Cluster.UVRanges[ UVIndex ].Min = UVMin;
		Cluster.UVRanges[ UVIndex ].Scale = FVector2D(UVDelta.X / NU, UVDelta.Y / NV);
		Cluster.UVRanges[ UVIndex ].GapStart[0] = GapStartU;
		Cluster.UVRanges[ UVIndex ].GapStart[1] = GapStartV;
		Cluster.UVRanges[ UVIndex ].GapLength[0] = GapLengthU;
		Cluster.UVRanges[ UVIndex ].GapLength[1] = GapLengthV;
	}

	// Quantize and write positions
	for (uint32 VertexIndex = 0; VertexIndex < NumClusterVerts; VertexIndex++)
	{
		const FUIntVector& QuantizedPosition = Cluster.QuantizedPositions[VertexIndex];
		uint32 PackedPosition = (QuantizedPosition.Z << (POSITION_QUANTIZATION_BITS * 2)) | (QuantizedPosition.Y << (POSITION_QUANTIZATION_BITS)) | (QuantizedPosition.X << 0);
		//BitWriter.PutBits(PackedPosition, 3 * POSITION_QUANTIZATION_BITS);
		BitWriter_Position.PutBits(PackedPosition, 32);
	}
	BitWriter_Position.Flush(sizeof(uint32));

	// Quantize and write remaining shading attributes
	for (uint32 VertexIndex = 0; VertexIndex < NumClusterVerts; VertexIndex++)
	{
		// Normal
		uint32 PackedNormal = PackNormal(Meshlet.Verts[VertexIndex].Normal, NORMAL_QUANTIZATION_BITS);

		BitWriter_Attribute.PutBits(PackedNormal, 2 * NORMAL_QUANTIZATION_BITS);

		// UVs
		for (uint32 TexCoordIndex = 0; TexCoordIndex < NumTexCoords; TexCoordIndex++)
		{
			uint32 PackedUV = PackedUVs[ NumClusterVerts * TexCoordIndex + VertexIndex ];
			BitWriter_Attribute.PutBits(PackedUV, 2 * TEXCOORD_QUANTIZATION_BITS);
		}
	}
	BitWriter_Attribute.Flush(sizeof(uint32));

	// Set offsets to where they are relative to the start of GeometryData
	Cluster.IndexSize = IndexData.Num();
	Cluster.PositionSize = PositionData.Num();
	Cluster.AttributeSize = AttributeData.Num();

	Cluster.IndexOffset = 0;
	Cluster.PositionOffset = 0;
	Cluster.AttributeOffset = 0;
	
	Cluster.BitsPerAttrib = 2 * NORMAL_QUANTIZATION_BITS + NumTexCoords * 2 * TEXCOORD_QUANTIZATION_BITS;
}

// Generate a permutation of cluster groups that is sorted first by mip level and then by Morton order x, y and z.
// Sorting by mip level first ensure that there can be no cyclic dependencies between formed pages.
static TArray<uint32> CalculateClusterGroupPermutation( const TArray< FClusterGroup >& ClusterGroups )
{
	struct FClusterGroupSortEntry {
		int32	MipLevel;
		uint32	MortonXYZ;
		uint32	OldIndex;
	};

	uint32 NumClusterGroups = ClusterGroups.Num();
	TArray< FClusterGroupSortEntry > ClusterGroupSortEntries;
	ClusterGroupSortEntries.SetNumUninitialized( NumClusterGroups );

	FVector MinCenter = FVector( FLT_MAX, FLT_MAX, FLT_MAX );
	FVector MaxCenter = FVector( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	for( const FClusterGroup ClusterGroup : ClusterGroups )
	{
		const FVector& Center = ClusterGroup.LODBounds.Center;
		MinCenter = FVector::Min( MinCenter, Center );
		MaxCenter = FVector::Max( MaxCenter, Center );
	}

	for( uint32 i = 0; i < NumClusterGroups; i++ )
	{
		const FClusterGroup& ClusterGroup = ClusterGroups[ i ];
		FClusterGroupSortEntry& SortEntry = ClusterGroupSortEntries[ i ];
		const FVector& Center = ClusterGroup.LODBounds.Center;
		const FVector ScaledCenter = ( Center - MinCenter ) / ( MaxCenter - MinCenter ) * 1023.0f + 0.5f;
		uint32 X = FMath::Clamp( (int32)ScaledCenter.X, 0, 1023 );
		uint32 Y = FMath::Clamp( (int32)ScaledCenter.Y, 0, 1023 );
		uint32 Z = FMath::Clamp( (int32)ScaledCenter.Z, 0, 1023 );

		SortEntry.MipLevel = ClusterGroup.MipLevel;
		SortEntry.MortonXYZ = ( FMath::MortonCode3(Z) << 2 ) | ( FMath::MortonCode3(Y) << 1 ) | FMath::MortonCode3(X);
		SortEntry.OldIndex = i;
	}

	ClusterGroupSortEntries.Sort( []( const FClusterGroupSortEntry& A, const FClusterGroupSortEntry& B ) {
		if( A.MipLevel != B.MipLevel )
			return A.MipLevel > B.MipLevel;
		return A.MortonXYZ < B.MortonXYZ;
	} );

	TArray<uint32> Permutation;
	Permutation.SetNumUninitialized( NumClusterGroups );
	for( uint32 i = 0; i < NumClusterGroups; i++ )
		Permutation[ i ] = ClusterGroupSortEntries[ i ].OldIndex;
	return Permutation;
}

/*
	Build streaming pages
	Page layout:
		Fixup Chunk (Only loaded to CPU memory)
		FPackedTriCluster
		MaterialRangeTable
		GeometryData
*/

static void EncodeClustersAndAssignToPages(
	Nanite::FResources& Resources,
	TArray< FClusterGroup >& ClusterGroups,
	TArray< Nanite::FTriCluster >& Clusters,
	const TArray< Nanite::FMeshlet >& Meshlets,
	uint32 NumTexCoords,
	TArray<FPage>& Pages,
	TArray<FClusterGroupPart>& Parts
	)
{
	check(Pages.Num() == 0);
	check(Parts.Num() == 0);

	const uint32 NumClusterGroups = ClusterGroups.Num();
	Pages.AddDefaulted();

	TArray<uint32> ClusterGroupPermutation = CalculateClusterGroupPermutation(ClusterGroups);

	for (uint32 i = 0; i < NumClusterGroups; i++)
	{
		// Pick best next group			// TODO
		uint32 GroupIndex = ClusterGroupPermutation[i];
		FClusterGroup& Group = ClusterGroups[GroupIndex];
		uint32 GroupStartPage = INVALID_PAGE_INDEX;
	
		for (uint32 ClusterIndex : Group.Children)
		{
			// Pick best next cluster		// TODO
			FTriCluster& Cluster = Clusters[ClusterIndex];
			const FMeshlet& Meshlet = Meshlets[ClusterIndex];

			// Encode and add to page
			uint32 MaterialTableDwords = CalcMaterialTableSize(Cluster);

			TArray<uint8> IndexData;
			TArray<uint8> PositionData;
			TArray<uint8> AttributeData;
			EncodeGeometryData(Cluster, Meshlet, NumTexCoords, IndexData, PositionData, AttributeData);

			uint32 EncodedSize = sizeof(FPackedTriCluster) + MaterialTableDwords * sizeof(uint32) + IndexData.Num() + PositionData.Num() + AttributeData.Num();
			
			FPage* Page = &Pages.Top();
			if (Page->EncodedSize + EncodedSize > CLUSTER_PAGE_SIZE || Page->NumClusters + 1 > MAX_CLUSTERS_PER_PAGE)
			{
				// Page is full. Need to start a new one
				Pages.AddDefaulted();
				Page = &Pages.Top();

				// TODO: Re-encode cluster here when we have context-aware encoding
			}
			
			// Start a new part?
			if (Page->PartsNum == 0 || Parts[Page->PartsStartIndex + Page->PartsNum - 1].GroupIndex != GroupIndex)
			{
				if (Page->PartsNum == 0)
				{
					Page->PartsStartIndex = Parts.Num();
				}
				Page->PartsNum++;

				FClusterGroupPart& Part = Parts.AddDefaulted_GetRef();
				Part.GroupIndex = GroupIndex;
			}

			// Add cluster to page
			uint32 PageIndex = Pages.Num() - 1;
			uint32 PartIndex = Parts.Num() - 1;

			FClusterGroupPart& Part = Parts.Last();
			if (Part.Clusters.Num() == 0)
			{
				Part.PageClusterOffset = Page->NumClusters;
				Part.PageIndex = PageIndex;
			}
			Part.Clusters.Add(ClusterIndex);
			check(Part.Clusters.Num() <= MAX_CLUSTERS_PER_GROUP);

			Cluster.GroupPartIndex = PartIndex;
			
			if (GroupStartPage == INVALID_PAGE_INDEX)
			{
				GroupStartPage = PageIndex;
			}
			
			Page->EncodedSize += EncodedSize;
			Page->NumClusters++;
			
			Cluster.IndexOffset = Page->IndexData.Num();
			Cluster.PositionOffset = Page->PositionData.Num();
			Cluster.AttributeOffset = Page->AttributeData.Num();
			Page->IndexData.Append(IndexData);
			Page->PositionData.Append(PositionData);
			Page->AttributeData.Append(AttributeData);
		}

		Group.PageIndexStart = GroupStartPage;
		Group.PageIndexNum = Pages.Num() - GroupStartPage;
		check(Group.PageIndexNum >= 1);
		check(Group.PageIndexNum <= MAX_GROUP_PARTS_MASK);
	}

	// Recalculate bounds for group parts
	for (FClusterGroupPart& Part : Parts)
	{
		TArray< FSphere > BoundSpheres;
		BoundSpheres.SetNumUninitialized(Part.Clusters.Num());
		check(Part.Clusters.Num() <= MAX_CLUSTERS_PER_GROUP);
		check(Part.PageIndex < (uint32)Pages.Num());
		
		uint32 NumClusters = Part.Clusters.Num();
		for (uint32 i = 0; i < NumClusters; i++)
		{
			BoundSpheres[i] = Clusters[Part.Clusters[i]].SphereBounds;
		}
		Part.Bounds = FSphere(BoundSpheres.GetData(), BoundSpheres.Num());
	}

	uint32 TotalEncodedSize = 0;
	for (FPage& Page: Pages)
	{
		TotalEncodedSize += Page.EncodedSize;
	}
}

static void WritePages(	Nanite::FResources& Resources,
						TArray<FPage>& Pages,
						const TArray<FClusterGroup>& Groups,
						const TArray<FClusterGroupPart>& Parts,
						const TArray<Nanite::FTriCluster>& Clusters)
{
	check(Resources.PageStreamingStates.Num() == 0);

	TArray< uint8 > StreamableBulkData;
	
	const uint32 NumPages = Pages.Num();
	const uint32 NumClusters = Clusters.Num();
	Resources.PageStreamingStates.SetNum(NumPages);

	TArray<FFixupChunk> FixupChunks;
	FixupChunks.SetNum(NumPages);
	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const FPage& Page = Pages[PageIndex];
		FFixupChunk& FixupChunk = FixupChunks[PageIndex];
		FixupChunk.Header.NumClusters = Page.NumClusters;

		uint32 NumHierarchyFixups = 0;
		for (uint32 i = 0; i < Page.PartsNum; i++)
		{
			const FClusterGroupPart& Part = Parts[Page.PartsStartIndex + i];
			NumHierarchyFixups += Groups[Part.GroupIndex].PageIndexNum;
		}

		FixupChunk.Header.NumHierachyFixups = NumHierarchyFixups;	// NumHierarchyFixups must be set before writing cluster fixups
	}

	// Add external fixups to pages
	for (const FClusterGroupPart& Part : Parts)
	{
		check(Part.PageIndex < NumPages);

		const FClusterGroup& Group = Groups[Part.GroupIndex];
		for (uint32 ClusterPositionInPart = 0; ClusterPositionInPart < (uint32)Part.Clusters.Num(); ClusterPositionInPart++)
		{
			const FTriCluster& Cluster = Clusters[Part.Clusters[ClusterPositionInPart]];
			if (Cluster.GeneratingGroupIndex != INVALID_GROUP_INDEX)
			{
				const FClusterGroup& GeneratingGroup = Groups[Cluster.GeneratingGroupIndex];
				check(GeneratingGroup.PageIndexNum >= 1);

				if (GeneratingGroup.PageIndexStart == Part.PageIndex && GeneratingGroup.PageIndexNum == 1)
					continue;	// Dependencies already met by current page. Fixup directly instead.

				uint32 PageDependencyStart = GeneratingGroup.PageIndexStart;
				uint32 PageDependencyNum = GeneratingGroup.PageIndexNum;
				RemoveRootPagesFromRange(PageDependencyStart, PageDependencyNum);	// Root page should never be a dependency

				const FClusterFixup ClusterFixup = FClusterFixup(Part.PageIndex, Part.PageClusterOffset + ClusterPositionInPart, PageDependencyStart, PageDependencyNum);
				for (uint32 i = 0; i < GeneratingGroup.PageIndexNum; i++)
				{
					//TODO: Implement some sort of FFixupPart to not redundantly store PageIndexStart/PageIndexNum?
					FFixupChunk& FixupChunk = FixupChunks[GeneratingGroup.PageIndexStart + i];
					FixupChunk.GetClusterFixup(FixupChunk.Header.NumClusterFixups++) = ClusterFixup;
				}
			}
		}
	}

	for (uint32 PageIndex = 0; PageIndex < NumPages; PageIndex++)
	{
		const FPage& Page = Pages[PageIndex];

		// Fill fixup header
		FFixupChunk& FixupChunk = FixupChunks[PageIndex];

		// Add hierarchy fixups
		{
			// Parts include the hierarchy fixups for all the other parts of the same group.
			uint32 NumHierarchyFixups = 0;
			for (uint32 i = 0; i < Page.PartsNum; i++)
			{
				const FClusterGroupPart& Part = Parts[Page.PartsStartIndex + i];
				const FClusterGroup& Group = Groups[Part.GroupIndex];

				uint32 PageDependencyStart = Group.PageIndexStart;
				uint32 PageDependencyNum = Group.PageIndexNum;
				RemoveRootPagesFromRange(PageDependencyStart, PageDependencyNum);

				// Add fixups to all parts of the group
				for (uint32 j = 0; j < Group.PageIndexNum; j++)
				{
					const FPage& Page2 = Pages[Group.PageIndexStart + j];
					for (uint32 k = 0; k < Page2.PartsNum; k++)
					{
						const FClusterGroupPart& Part2 = Parts[Page2.PartsStartIndex + k];
						if (Part2.GroupIndex == Part.GroupIndex)
						{
							FixupChunk.GetHierarchyFixup(NumHierarchyFixups++) = FHierarchyFixup(Part2.PageIndex, Part2.HierarchyNodeIndex, Part2.HierarchyChildIndex, Part2.PageClusterOffset, PageDependencyStart, PageDependencyNum);
							break;
						}
					}
				}
			}
			check(NumHierarchyFixups == FixupChunk.Header.NumHierachyFixups);
		}

		// Generate page dependencies
		FPageStreamingState& PageStreamingState = Resources.PageStreamingStates[PageIndex];
		{
			PageStreamingState.DependenciesStart = Resources.PageDependencies.Num();
			for (uint32 i = 0; i < FixupChunk.Header.NumClusterFixups; i++)
			{
				uint32 FixupPageIndex = FixupChunk.GetClusterFixup(i).GetPageIndex();
				check(FixupPageIndex < NumPages);
				if (IsRootPage(FixupPageIndex) || FixupPageIndex == PageIndex)	// Never emit dependencies to ourselves or a root page.
					continue;

				// Only add if not already in the set.
				// O(n^2), but number of dependencies should be tiny in practice.
				bool bFound = false;
				for (uint32 j = PageStreamingState.DependenciesStart; j < (uint32)Resources.PageDependencies.Num(); j++)
				{
					if (Resources.PageDependencies[j] == FixupPageIndex)
					{
						bFound = true;
						break;
					}
				}

				if (bFound)
					continue;

				Resources.PageDependencies.Add(FixupPageIndex);
			}
			PageStreamingState.DependenciesNum = Resources.PageDependencies.Num() - PageStreamingState.DependenciesStart;
		}

		TArray<uint8>& BulkData = IsRootPage(PageIndex) ? Resources.RootClusterPage : StreamableBulkData;

		PageStreamingState.BulkOffset = BulkData.Num();

		uint32 FixupChunkSize = FixupChunk.GetSize();
		BulkData.Append((uint8*)&FixupChunk, FixupChunkSize);
		check(FixupChunk.Header.NumHierachyFixups < MAX_CLUSTERS_PER_PAGE);
		check(FixupChunk.Header.NumClusterFixups < MAX_CLUSTERS_PER_PAGE);

		for (uint32 i = 0; i < FixupChunk.Header.NumClusterFixups; i++)
		{
			FClusterFixup& Fixup = FixupChunk.GetClusterFixup(i);
			check(Fixup.GetPageDependencyNum() > 0);
		}

		uint32 GPUDataStartOffset = BulkData.Num();

		// Pack clusters and generate material range data
		TArray< FPackedTriCluster > PackedClusters;
		PackedClusters.SetNumUninitialized(Page.NumClusters);
		
		const uint32 NumPackedClusterDwords = Page.NumClusters * sizeof(FPackedTriCluster) / sizeof(uint32);
		TArray< uint32 > MaterialRangeData;
		for (uint32 i = 0; i < Page.PartsNum; i++)
		{
			const FClusterGroupPart& Part = Parts[Page.PartsStartIndex + i];
			for (uint32 j = 0; j < (uint32)Part.Clusters.Num(); j++)
			{
				const uint32 ClusterIndex = Part.Clusters[j];
				FPackedTriCluster& PackedCluster = PackedClusters[Part.PageClusterOffset + j];
				PackTriCluster(PackedCluster, Clusters[ClusterIndex]);
				PackedCluster.PackedMaterialInfo = PackMaterialInfo(Clusters[ClusterIndex], MaterialRangeData, NumPackedClusterDwords);
			}
		}

		// Perform page-internal fix up directly on PackedClusters
		for (uint32 LocalPartIndex = 0; LocalPartIndex < Page.PartsNum; LocalPartIndex++)
		{
			const FClusterGroupPart& Part = Parts[Page.PartsStartIndex + LocalPartIndex];
			const FClusterGroup& Group = Groups[Part.GroupIndex];
			for (uint32 ClusterPositionInPart = 0; ClusterPositionInPart < (uint32)Part.Clusters.Num(); ClusterPositionInPart++)
			{
				const FTriCluster& Cluster = Clusters[Part.Clusters[ClusterPositionInPart]];
				if (Cluster.GeneratingGroupIndex != INVALID_GROUP_INDEX)
				{
					const FClusterGroup& GeneratingGroup = Groups[Cluster.GeneratingGroupIndex];
					uint32 PageDependencyStart = Group.PageIndexStart;
					uint32 PageDependencyNum = Group.PageIndexNum;
					RemoveRootPagesFromRange(PageDependencyStart, PageDependencyNum);

					if (GeneratingGroup.PageIndexStart == PageIndex && GeneratingGroup.PageIndexNum == 1)
					{
						// Dependencies already met by current page. Fixup directly.
						PackedClusters[Part.PageClusterOffset + ClusterPositionInPart].Flags &= ~NANITE_CLUSTER_FLAG_LEAF;	// Mark parent as no longer leaf
					}
				}
			}
		}

		// Adjust IndexOffset/PositionOffset/AttribOffset
		{
			uint32 IndexOffset = PackedClusters.Num() * PackedClusters.GetTypeSize() + MaterialRangeData.Num() * MaterialRangeData.GetTypeSize();
			uint32 PositionOffset = IndexOffset + Page.IndexData.Num();
			uint32 AttributeOffset = PositionOffset + Page.PositionData.Num();
			for (FPackedTriCluster& PackedCluster : PackedClusters)
			{
				PackedCluster.IndexOffset += IndexOffset;
				PackedCluster.PositionOffset += PositionOffset;
				PackedCluster.AttributeOffset += AttributeOffset;
			}
		}
		
		// Write clusters in SOA layout
		uint32 PageStartOffset = BulkData.Num();
		const uint32 NumClusterFloat4Propeties = sizeof(FPackedTriCluster) / 16;
		for (uint32 float4Index = 0; float4Index < NumClusterFloat4Propeties; float4Index++)
		{
			for (const FPackedTriCluster& PackedCluster : PackedClusters)
			{
				BulkData.Append((uint8*)&PackedCluster + float4Index * 16, 16);
			}
		}
	
		// Write Material Range Data
		BulkData.Append((uint8*)MaterialRangeData.GetData(), MaterialRangeData.Num() * MaterialRangeData.GetTypeSize());

		// Write Indices
		BulkData.Append((uint8*)Page.IndexData.GetData(), Page.IndexData.Num());

		// Write Positions
		BulkData.Append((uint8*)Page.PositionData.GetData(), Page.PositionData.Num());

		// Write Attributes
		BulkData.Append((uint8*)Page.AttributeData.GetData(), Page.AttributeData.Num());

		uint32 GPUDataSize = BulkData.Num() - GPUDataStartOffset;
		check(GPUDataSize <= CLUSTER_PAGE_SIZE);

		PageStreamingState.BulkSize = BulkData.Num() - PageStreamingState.BulkOffset;
	}
	
	// FPlatformMisc::LowLevelOutputDebugStringf(TEXT("%d bytes of page data in %d pages. %.3f bytes per page. %.3f utilization.\n"), TotalBytes, NumPages, TotalBytes / double(NumPages), TotalBytes / double(NumPages) * CLUSTER_PAGE_SIZE);
	uint32 TotalBytes = Resources.RootClusterPage.Num() + StreamableBulkData.Num();
	UE_LOG(LogStaticMesh, Log, TEXT("%d bytes of page data written in %d pages. %.3f bytes per page. %.3f%% utilization.\n"), TotalBytes, NumPages, TotalBytes / float(NumPages), TotalBytes / (float(NumPages) * CLUSTER_PAGE_SIZE) * 100.0f);

	// Store PageData
	Resources.StreamableClusterPages.Lock(LOCK_READ_WRITE);
	uint8* Ptr = (uint8*)Resources.StreamableClusterPages.Realloc(StreamableBulkData.Num());
	FMemory::Memcpy(Ptr, StreamableBulkData.GetData(), StreamableBulkData.Num());
	Resources.StreamableClusterPages.Unlock();
	Resources.StreamableClusterPages.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
}

static FSphere CombineBoundingSpheres( const FSphere& SphereA, const FSphere& SphereB )
{
	FVector Delta = SphereB.Center - SphereA.Center;
	float Dist2 = FVector::DotProduct( Delta, Delta );
	float DeltaR = SphereB.W - SphereA.W;

	if( DeltaR * DeltaR >= Dist2 )
	{
		// Larger sphere encloses smaller sphere
		return SphereA.W >= SphereB.W ? SphereA : SphereB;
	}
	else
	{
		// Partially overlapping or disjoin
		float Dist = FMath::Sqrt( Dist2 );
		FSphere Result = SphereA;
		Result.W = ( Dist + SphereA.W + SphereB.W ) * 0.5f;
		if( Dist > 1e-5f )
		{
			Result.Center += ( Result.W - SphereA.W ) / Dist * Delta;
		}
		return Result;
	}
}

struct FIntermediateNode
{
	enum class EType : uint8
	{
		GroupPart,
		Leaf,
		InnerNode
	};

	FSphere				Bound;
	uint32				Index;
	EType				Type;
	TArray< uint32 >	Children;
	
	bool operator<(const FIntermediateNode& Node) const
	{
		return Bound.W < Node.Bound.W;
	}
};

static uint32 BuildHierarchyNodesKMeansRecursive( TArray< Nanite::FHierarchyNode >& HierarchyNodes, const TArray< FIntermediateNode >& Nodes, const TArray<Nanite::FClusterGroup>& Groups, TArray< Nanite::FClusterGroupPart >& Parts, uint32 CurrentNodeIndex )
{
	const FIntermediateNode& INode = Nodes[ CurrentNodeIndex ];
	check( INode.Index == MAX_uint32 );
	check( INode.Type == FIntermediateNode::EType::Leaf || INode.Type == FIntermediateNode::EType::InnerNode );

	uint32 HNodeIndex = HierarchyNodes.Num();
	HierarchyNodes.AddZeroed();

	uint32 NumChildren = INode.Children.Num();
	check( NumChildren > 0 && NumChildren <= 64 );
	for( uint32 ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++ )
	{
		uint32 ChildNodeIndex = INode.Children[ ChildIndex ];
		const FIntermediateNode& ChildNode = Nodes[ ChildNodeIndex ];
		if( ChildNode.Type == FIntermediateNode::EType::Leaf || ChildNode.Type == FIntermediateNode::EType::InnerNode )
		{
			// Hierarchy node
			uint32 ChildHierarchyNodeIndex = BuildHierarchyNodesKMeansRecursive( HierarchyNodes, Nodes, Groups, Parts, ChildNodeIndex );
			
			const Nanite::FHierarchyNode& ChildHNode = HierarchyNodes[ ChildHierarchyNodeIndex ];

			TArray< FSphere, TInlineAllocator<64> > BoundSpheres;
			TArray< FSphere, TInlineAllocator<64> > LODBoundSpheres;
			float MinLODError = MAX_flt;
			float MaxLODError = 0.0f;
			for( uint32 GrandChildIndex = 0; GrandChildIndex < 64 && ChildHNode.NumChildren[ GrandChildIndex ] != 0; GrandChildIndex++ )
			{
				BoundSpheres.Add( ChildHNode.Bounds[ GrandChildIndex ] );
				LODBoundSpheres.Add( ChildHNode.LODBounds[ GrandChildIndex ] );
				MinLODError = FMath::Min( MinLODError, ChildHNode.MinLODErrors[ GrandChildIndex ] );
				MaxLODError = FMath::Max( MaxLODError, ChildHNode.MaxLODErrors[ GrandChildIndex ] );
			}

			FSphere Bounds = FSphere( BoundSpheres.GetData(), BoundSpheres.Num() );
			FSphere LODBounds = FSphere( LODBoundSpheres.GetData(), LODBoundSpheres.Num() );

			Nanite::FHierarchyNode& HNode = HierarchyNodes[ HNodeIndex ];
			HNode.Bounds[ ChildIndex ] = Bounds;
			HNode.LODBounds[ ChildIndex ] = LODBounds;
			HNode.MinLODErrors[ ChildIndex ] = MinLODError;
			HNode.MaxLODErrors[ ChildIndex ] = MaxLODError;
			HNode.ChildrenStartIndex[ ChildIndex ] = ChildHierarchyNodeIndex;
			HNode.NumChildren[ ChildIndex ] = MAX_CLUSTERS_PER_GROUP;
			HNode.ClusterGroupPartIndex[ ChildIndex ] = INVALID_GROUP_INDEX;
		}
		else
		{
			// Cluster Group
			check( ChildNode.Type == FIntermediateNode::EType::GroupPart );
			FClusterGroupPart& Part = Parts[ ChildNode.Index ];
			const FClusterGroup& Group = Groups[Part.GroupIndex];

			FHierarchyNode& HNode = HierarchyNodes[ HNodeIndex ];
			HNode.Bounds[ ChildIndex ] = Part.Bounds;
			HNode.LODBounds[ ChildIndex ] = Group.LODBounds;
			HNode.MinLODErrors[ ChildIndex ] = Group.MinLODError;
			HNode.MaxLODErrors[ ChildIndex ] = Group.MaxLODError;
			HNode.ChildrenStartIndex[ ChildIndex ] = 0xFFFFFFFFu;
			HNode.NumChildren[ ChildIndex ] = Part.Clusters.Num();
			HNode.ClusterGroupPartIndex[ ChildIndex ] = ChildNode.Index;

			check( HNode.NumChildren[ ChildIndex ] <= MAX_CLUSTERS_PER_GROUP );
			Part.HierarchyNodeIndex = HNodeIndex;
			Part.HierarchyChildIndex = ChildIndex;
		}
	}

	return HNodeIndex;
}

static void BuildHierarchyNodesKMeans( TArray< Nanite::FHierarchyNode >& HierarchyNodes, const TArray<Nanite::FClusterGroup>& Groups, TArray<Nanite::FClusterGroupPart>& Parts )
{
	struct FCluster
	{
		FVector	Center;
		FVector NextCenter;
		TArray< uint32 > Nodes;
	};

	srand(1234);
	const uint32 NumParts = Parts.Num();

	TArray< FIntermediateNode >	Nodes;
	Nodes.AddDefaulted( NumParts );
	for (uint32 i = 0; i < NumParts; i++)
	{
		Nodes[i].Bound = Parts[i].Bounds;
		Nodes[i].Index = i;
		Nodes[i].Type = FIntermediateNode::EType::GroupPart;
	}

	uint32 NodeBaseIndex = 0;
	uint32 NumLevelInputNodes = NumParts;

	while( NumLevelInputNodes > 64 )
	{
		uint32 NumLevelInputClusters = ( NumLevelInputNodes + 63 ) / 64;
		uint32 NumLevelOutputClusters = NumLevelInputClusters * 7 / 8;		// TODO: Ad-hoc. Can we tweak this somehow?
		uint32 NumLevelOutputNodes = NumLevelOutputClusters * 64;

		Sort( Nodes.GetData() + NodeBaseIndex, NumLevelOutputNodes );
	
		TArray< FCluster > Clusters;
		Clusters.AddDefaulted( NumLevelOutputClusters );
		for (uint32 i = 0; i < NumLevelOutputClusters; i++)
		{
			uint32 Idx = ( ( rand() << 15 ) ^ rand() ) % NumLevelOutputNodes;
			Clusters[ i ].Center = Nodes[ NodeBaseIndex + Idx ].Bound.Center;
			Clusters[ i ].NextCenter = FVector::ZeroVector;
		}

		const uint32 NUM_ITERATIONS = 10;
		for (uint32 Iteration = 1; Iteration <= NUM_ITERATIONS; Iteration++)
		{
			// Clear Clusters
			for( uint32 ClusterIndex = 0; ClusterIndex < NumLevelOutputClusters; ClusterIndex++ )
			{
				FCluster& Cluster = Clusters[ ClusterIndex ];
				Cluster.NextCenter = FVector::ZeroVector;
				Cluster.Nodes.Empty();
			}

			// Add Nodes to nearest cluster
			for( uint32 NodeIndex = 0; NodeIndex < NumLevelOutputNodes; NodeIndex++ )
			{
				const FIntermediateNode& Node = Nodes[ NodeBaseIndex + NodeIndex ];
				FVector NodeCenter = Node.Bound.Center;
				float NodeRadius = Node.Bound.W;

				float BestCost = FLT_MAX;
				uint32 BestCluster = MAX_uint32;
				for( uint32 ClusterIndex = 0; ClusterIndex < NumLevelOutputClusters; ClusterIndex++ )
				{
					const FCluster& Cluster = Clusters[ ClusterIndex ];

					if( Cluster.Nodes.Num() >= 64 )
						continue;

					FVector Delta = NodeCenter - Cluster.Center;
					float Cost = FVector::DotProduct( Delta, Delta );
					if (Cost < BestCost)
					{
						BestCost = Cost;
						BestCluster = ClusterIndex;
					}
				}
				if( BestCluster == MAX_uint32 )
				{
					BestCluster = NodeIndex % NumLevelOutputClusters;
				}
				Clusters[ BestCluster ].NextCenter += NodeCenter;
				Clusters[ BestCluster ].Nodes.Add( NodeIndex );
			}

			// Recalculate Centers
			for( uint32 ClusterIndex = 0; ClusterIndex < NumLevelOutputClusters; ClusterIndex++ )
			{
				FCluster& Cluster = Clusters[ ClusterIndex ];
				Cluster.Center = Cluster.NextCenter * (1.0f / 64.0f);
			}
		}

		Nodes.AddDefaulted( NumLevelOutputClusters );

		uint32 NewNodeBaseIndex = NodeBaseIndex + NumLevelInputNodes;
		for( uint32 ClusterIndex = 0; ClusterIndex < NumLevelOutputClusters; ClusterIndex++ )
		{
			const FCluster& Cluster = Clusters[ ClusterIndex ];
			FIntermediateNode& Node = Nodes[ NewNodeBaseIndex + ClusterIndex ];
			Node.Index = MAX_uint32;
			Node.Type = FIntermediateNode::EType::Leaf;

			uint32 NumChildren = Cluster.Nodes.Num();
			check( NumChildren == 64 );
			Node.Children.AddDefaulted( NumChildren );
			Node.Bound = Nodes[ NodeBaseIndex + Cluster.Nodes[ 0 ] ].Bound;

			for( uint32 i = 0; i < NumChildren; i++ )
			{
				Node.Children[ i ] = NodeBaseIndex + Cluster.Nodes[ i ];
				const FIntermediateNode& ChildNode = Nodes[ Node.Children[ i ] ];
				Node.Bound = CombineBoundingSpheres( Node.Bound, ChildNode.Bound );
				if( ChildNode.Type != FIntermediateNode::EType::GroupPart )
					Node.Type = FIntermediateNode::EType::InnerNode;
			}
		}
		
		NodeBaseIndex += NumLevelOutputNodes;
		NumLevelInputNodes = NumLevelInputNodes - NumLevelOutputNodes + NumLevelOutputClusters;
	}

	// Insert root node if necessary
	if( NumLevelInputNodes > 1 || NumParts == 1 )
	{
		FIntermediateNode Node;
		Node.Children.AddUninitialized( NumLevelInputNodes );
		Node.Index = MAX_uint32;
		Node.Type = FIntermediateNode::EType::Leaf;

		Node.Bound = Nodes[ NodeBaseIndex ].Bound;
		for( uint32 i = 0; i < NumLevelInputNodes; i++ )
		{
			Node.Children[ i ] = NodeBaseIndex + i;
			const FIntermediateNode& ChildNode = Nodes[ NodeBaseIndex + i ];
			Node.Bound = CombineBoundingSpheres( Node.Bound, ChildNode.Bound );
			if( ChildNode.Type != FIntermediateNode::EType::GroupPart )
				Node.Type = FIntermediateNode::EType::InnerNode;
		}

		check( Node.Children.Num() > 0 && Node.Children.Num() <= 64 );

		Nodes.Add( Node );
	}

	check(Nodes.Num() > 0);

	HierarchyNodes.Empty();
	BuildHierarchyNodesKMeansRecursive( HierarchyNodes, Nodes, Groups, Parts, Nodes.Num() - 1 );
}

static void BuildMaterialRanges(
	const TArray<uint32>& TriangleIndices,
	const TArray<int32>& MaterialIndices,
	TArray<FMaterialTriangle, TInlineAllocator<128>>& MaterialTris,
	TArray<FMaterialRange, TInlineAllocator<4>>& MaterialRanges)
{
	check(MaterialTris.Num() == 0);
	check(MaterialRanges.Num() == 0);
	check(MaterialIndices.Num() * 3 == TriangleIndices.Num());

	const uint32 TriangleCount = MaterialIndices.Num();

	TArray<uint32, TInlineAllocator<64>> MaterialCounts;
	MaterialCounts.AddZeroed(64);

	// Tally up number tris per material index
	for (uint32 i = 0; i < TriangleCount; i++)
	{
		const uint32 MaterialIndex = MaterialIndices[i];
		++MaterialCounts[MaterialIndex];
	}

	for (uint32 i = 0; i < TriangleCount; i++)
	{
		FMaterialTriangle MaterialTri;
		MaterialTri.Index0 = TriangleIndices[(i * 3) + 0];
		MaterialTri.Index1 = TriangleIndices[(i * 3) + 1];
		MaterialTri.Index2 = TriangleIndices[(i * 3) + 2];
		MaterialTri.MaterialIndex = MaterialIndices[i];
		MaterialTri.RangeCount = MaterialCounts[MaterialTri.MaterialIndex];
		check(MaterialTri.RangeCount > 0);
		MaterialTris.Add(MaterialTri);
	}

	// Sort by triangle range count descending, and material index ascending.
	MaterialTris.Sort(FCompareMaterialTriangleIndex());

	FMaterialRange CurrentRange;
	CurrentRange.RangeStart = 0;
	CurrentRange.RangeLength = 0;
	CurrentRange.MaterialIndex = MaterialTris.Num() > 0 ? MaterialTris[0].MaterialIndex : 0;

	for (int32 TriIndex = 0; TriIndex < MaterialTris.Num(); ++TriIndex)
	{
		const FMaterialTriangle& Triangle = MaterialTris[TriIndex];

		// Material changed, so add current range and reset
		if (CurrentRange.RangeLength > 0 && Triangle.MaterialIndex != CurrentRange.MaterialIndex)
		{
			MaterialRanges.Add(CurrentRange);

			CurrentRange.RangeStart = TriIndex;
			CurrentRange.RangeLength = 1;
			CurrentRange.MaterialIndex = Triangle.MaterialIndex;
		}
		else
		{
			++CurrentRange.RangeLength;
		}
	}

	// Add last triangle to range
	if (CurrentRange.RangeLength > 0)
	{
		MaterialRanges.Add(CurrentRange);
	}

	check(MaterialTris.Num() == TriangleCount);
}

static void BuildMaterialRanges(FTriCluster& Cluster, FMeshlet& Meshlet)
{
	check(Cluster.MaterialRanges.Num() == 0);
	check(Cluster.NumTris <= MAX_CLUSTER_TRIANGLES);
	check(Cluster.NumTris * 3 == Meshlet.Indexes.Num());

	TArray<FMaterialTriangle, TInlineAllocator<128>> MaterialTris;
	
	BuildMaterialRanges(
		Meshlet.Indexes,
		Meshlet.MaterialIndexes,
		MaterialTris,
		Cluster.MaterialRanges);

	// Write indices back to meshlets
	for (uint32 Triangle = 0; Triangle < Cluster.NumTris; ++Triangle)
	{
		Meshlet.Indexes[Triangle * 3 + 0] = MaterialTris[Triangle].Index0;
		Meshlet.Indexes[Triangle * 3 + 1] = MaterialTris[Triangle].Index1;
		Meshlet.Indexes[Triangle * 3 + 2] = MaterialTris[Triangle].Index2;
		Meshlet.MaterialIndexes[Triangle] = MaterialTris[Triangle].MaterialIndex;
	}
}

// Sort meshlet triangles into material ranges. Add Material ranges to clusters.
static void BuildMaterialRanges( TArray<FTriCluster>& Clusters, TArray<FMeshlet>& Meshlets )
{
	//const uint32 NumClusters = Clusters.Num();
	//for( uint32 ClusterIndex = 0; ClusterIndex < NumClusters; ClusterIndex++ )
	ParallelFor( Clusters.Num(),
		[&]( uint32 ClusterIndex )
		{
			BuildMaterialRanges( Clusters[ ClusterIndex ], Meshlets[ ClusterIndex ] );
		} );
}

// Prints material range stats. This has to happen separate from BuildMaterialRanges as materials might be recalculated because of cluster splitting.
static void PrintMaterialRangeStats( TArray<FTriCluster>& Clusters, TArray<FMeshlet>& Meshlets )
{
	TFixedBitVector<MAX_CLUSTER_MATERIALS> UsedMaterialIndices;
	UsedMaterialIndices.Clear();

	uint32 NumClusterMaterials[ 4 ] = { 0, 0, 0, 0 }; // 1, 2, 3, >= 4

	const uint32 NumClusters = Clusters.Num();
	for( uint32 ClusterIndex = 0; ClusterIndex < NumClusters; ClusterIndex++ )
	{
		FTriCluster& Cluster = Clusters[ ClusterIndex ];
		FMeshlet& Meshlet = Meshlets[ ClusterIndex ];

		// TODO: Valid assumption? All null materials should have been assigned default material at this point.
		check( Cluster.MaterialRanges.Num() > 0 );
		NumClusterMaterials[ FMath::Min( Cluster.MaterialRanges.Num() - 1, 3 ) ]++;

		for( const FMaterialRange& MaterialRange : Cluster.MaterialRanges )
		{
			UsedMaterialIndices.SetBit( MaterialRange.MaterialIndex );
		}
	}

	UE_LOG( LogStaticMesh, Log, TEXT( "Material Stats - Unique Materials: %d, Fast Path Clusters: %d, Slow Path Clusters: %d, 1 Material: %d, 2 Materials: %d, 3 Materials: %d, At Least 4 Materials: %d" ),
		UsedMaterialIndices.CountBits(), Clusters.Num() - NumClusterMaterials[ 3 ], NumClusterMaterials[ 3 ], NumClusterMaterials[ 0 ], NumClusterMaterials[ 1 ], NumClusterMaterials[ 2 ], NumClusterMaterials[ 3 ] );

#if 0
	for( uint32 MaterialIndex = 0; MaterialIndex < MAX_CLUSTER_MATERIALS; ++MaterialIndex )
	{
		if( UsedMaterialIndices.GetBit( MaterialIndex ) > 0 )
		{
			UE_LOG( LogStaticMesh, Log, TEXT( "  Material Index: %d" ), MaterialIndex );
		}
	}
#endif
}

#if DO_CHECK
static void VerifyClusterConstaints( const FTriCluster& Cluster, const FMeshlet& Meshlet )
{
	check( Cluster.NumVerts == Meshlet.Verts.Num() );
	check( Cluster.NumTris * 3 == Meshlet.Indexes.Num() );
	check( Cluster.NumVerts <= 256 );

	const uint32 NumTriangles = Cluster.NumTris;

	uint32 MaxVertexIndex = 0;
	for( uint32 i = 0; i < NumTriangles; i++ )
	{
		uint32 Index0 = Meshlet.Indexes[ i * 3 + 0 ];
		uint32 Index1 = Meshlet.Indexes[ i * 3 + 1 ];
		uint32 Index2 = Meshlet.Indexes[ i * 3 + 2 ];
		MaxVertexIndex = FMath::Max( MaxVertexIndex, FMath::Max3( Index0, Index1, Index2 ) );
		check( MaxVertexIndex - Index0 < CONSTRAINED_CLUSTER_CACHE_SIZE );
		check( MaxVertexIndex - Index1 < CONSTRAINED_CLUSTER_CACHE_SIZE );
		check( MaxVertexIndex - Index2 < CONSTRAINED_CLUSTER_CACHE_SIZE );
	}
}
#endif

// Weights for individual cache entries based on simulated annealing optimization on DemoLevel.
static int16 CacheWeightTable[ CONSTRAINED_CLUSTER_CACHE_SIZE ] = {
	 577,	 616,	 641,  512,		 614,  635,  478,  651,
	  65,	 213,	 719,  490,		 213,  726,  863,  745,
	 172,	 939,	 805,  885,		 958, 1208, 1319, 1318,
	1475,	1779,	2342,  159,		2307, 1998, 1211,  932
};

// Constrain cluster to only use vertex references that are within a fixed sized trailing window from the current highest encountered vertex index.
// Triangles are reordered based on a FIFO-style cache optimization to minimize the number of vertices that need to be duplicated.
static void ConstrainClusterFIFO( FTriCluster& Cluster, FMeshlet& Meshlet )
{
	uint32 NumOldTriangles = Cluster.NumTris;
	uint32 NumOldVertices = Cluster.NumVerts;

	const uint32 MAX_CLUSTER_TRIANGLES_IN_DWORDS = ( MAX_CLUSTER_TRIANGLES + 31 ) / 32;

	uint32 VertexToTriangleMasks[ MAX_CLUSTER_TRIANGLES * 3 ][ MAX_CLUSTER_TRIANGLES_IN_DWORDS ] = { };

	// Generate vertex to triangle masks
	for( uint32 i = 0; i < NumOldTriangles; i++ )
	{
		uint32 i0 = Meshlet.Indexes[ i * 3 + 0 ];
		uint32 i1 = Meshlet.Indexes[ i * 3 + 1 ];
		uint32 i2 = Meshlet.Indexes[ i * 3 + 2 ];
		check( i0 != i1 && i1 != i2 && i2 != i0 ); // Degenerate input triangle!

		VertexToTriangleMasks[ i0 ][ i >> 5 ] |= 1 << ( i & 31 );
		VertexToTriangleMasks[ i1 ][ i >> 5 ] |= 1 << ( i & 31 );
		VertexToTriangleMasks[ i2 ][ i >> 5 ] |= 1 << ( i & 31 );
	}

	uint32 TrianglesEnabled[ MAX_CLUSTER_TRIANGLES_IN_DWORDS ] = {};	// Enabled triangles are in the current material range and have not yet been visited.
	uint32 TrianglesTouched[ MAX_CLUSTER_TRIANGLES_IN_DWORDS ] = {};	// Touched triangles have had at least one of their vertices visited.

	uint16 OptimizedIndices[ MAX_CLUSTER_TRIANGLES * 3 ];

	uint32 NumNewVertices = 0;
	uint32 NumNewTriangles = 0;
	uint16 OldToNewVertex[ MAX_CLUSTER_TRIANGLES * 3 ];
	uint16 NewToOldVertex[ MAX_CLUSTER_TRIANGLES * 3 ];
	FMemory::Memset( OldToNewVertex, -1, sizeof( OldToNewVertex ) );

	auto ScoreVertex = [ &OldToNewVertex, &NumNewVertices ] ( uint32 OldVertex )
	{
		uint16 NewIndex = OldToNewVertex[ OldVertex ];

		int32 CacheScore = 0;
		if( NewIndex != 0xFFFF )
		{
			uint32 CachePosition = ( NumNewVertices - 1 ) - NewIndex;
			if( CachePosition < CONSTRAINED_CLUSTER_CACHE_SIZE )
				CacheScore = CacheWeightTable[ CachePosition ];
		}

		return CacheScore;
	};

	uint32 RangeStart = 0;
	for( FMaterialRange& MaterialRange : Cluster.MaterialRanges )
	{
		check( RangeStart == MaterialRange.RangeStart );
		uint32 RangeLength = MaterialRange.RangeLength;

		// Enable triangles from current range
		for( uint32 i = 0; i < MAX_CLUSTER_TRIANGLES_IN_DWORDS; i++ )
		{
			int32 RangeStartRelativeToDword = (int32)RangeStart - (int32)i * 32;
			int32 BitStart = FMath::Max( RangeStartRelativeToDword, 0 );
			int32 BitEnd = FMath::Max( RangeStartRelativeToDword + (int32)RangeLength, 0 );
			uint32 StartMask = BitStart < 32 ? ( ( 1u << BitStart ) - 1u ) : 0xFFFFFFFFu;
			uint32 EndMask = BitEnd < 32 ? ( ( 1u << BitEnd ) - 1u ) : 0xFFFFFFFFu;
			TrianglesEnabled[ i ] |= StartMask ^ EndMask;
		}

		while( true )
		{
			uint32 NextTriangleIndex = 0xFFFF;
			int32 NextTriangleScore = 0;

			// Pick highest scoring available triangle
			for( uint32 TriangleDwordIndex = 0; TriangleDwordIndex < MAX_CLUSTER_TRIANGLES_IN_DWORDS; TriangleDwordIndex++ )
			{
				uint32 CandidateMask = TrianglesTouched[ TriangleDwordIndex ] & TrianglesEnabled[ TriangleDwordIndex ];
				while( CandidateMask )
				{
					uint32 TriangleDwordOffset = FMath::CountTrailingZeros( CandidateMask );
					CandidateMask &= CandidateMask - 1;

					int32 TriangleIndex = ( TriangleDwordIndex << 5 ) + TriangleDwordOffset;

					int32 TriangleScore = 0;
					TriangleScore += ScoreVertex( Meshlet.Indexes[ TriangleIndex * 3 + 0 ] );
					TriangleScore += ScoreVertex( Meshlet.Indexes[ TriangleIndex * 3 + 1 ] );
					TriangleScore += ScoreVertex( Meshlet.Indexes[ TriangleIndex * 3 + 2 ] );

					if( TriangleScore > NextTriangleScore )
					{
						NextTriangleIndex = TriangleIndex;
						NextTriangleScore = TriangleScore;
					}
				}
			}

			if( NextTriangleIndex == 0xFFFF )
			{
				// If we didn't find a triangle. It might be because it is part of a separate component. Look for an unvisited triangle to restart from.
				for( uint32 TriangleDwordIndex = 0; TriangleDwordIndex < MAX_CLUSTER_TRIANGLES_IN_DWORDS; TriangleDwordIndex++ )
				{
					uint32 EnableMask = TrianglesEnabled[ TriangleDwordIndex ];
					if( EnableMask )
					{
						NextTriangleIndex = ( TriangleDwordIndex << 5 ) + FMath::CountTrailingZeros( EnableMask );
						break;
					}
				}

				if( NextTriangleIndex == 0xFFFF )
					break;
			}

			uint32 OldIndex0 = Meshlet.Indexes[ NextTriangleIndex * 3 + 0 ];
			uint32 OldIndex1 = Meshlet.Indexes[ NextTriangleIndex * 3 + 1 ];
			uint32 OldIndex2 = Meshlet.Indexes[ NextTriangleIndex * 3 + 2 ];

			// Mark incident triangles
			for( uint32 i = 0; i < MAX_CLUSTER_TRIANGLES_IN_DWORDS; i++ )
			{
				TrianglesTouched[ i ] |= VertexToTriangleMasks[ OldIndex0 ][ i ] | VertexToTriangleMasks[ OldIndex1 ][ i ] | VertexToTriangleMasks[ OldIndex2 ][ i ];
			}

			uint16& NewIndex0 = OldToNewVertex[ OldIndex0 ];
			uint16& NewIndex1 = OldToNewVertex[ OldIndex1 ];
			uint16& NewIndex2 = OldToNewVertex[ OldIndex2 ];

			// Generate new indices such that they are all within a trailing window of CONSTRAINED_CLUSTER_CACHE_SIZE of NumNewVertices.
			// This can require multiple iterations as new/duplicate vertices can push other vertices outside the window.
			uint32 PrevNumVewVertices;
			do
			{
				PrevNumVewVertices = NumNewVertices;
				if( NewIndex0 == 0xFFFF || NumNewVertices - NewIndex0 >= CONSTRAINED_CLUSTER_CACHE_SIZE )
				{
					NewIndex0 = NumNewVertices++;	NewToOldVertex[ NewIndex0 ] = OldIndex0;
				}
				if( NewIndex1 == 0xFFFF || NumNewVertices - NewIndex1 >= CONSTRAINED_CLUSTER_CACHE_SIZE )
				{
					NewIndex1 = NumNewVertices++;	NewToOldVertex[ NewIndex1 ] = OldIndex1;
				}
				if( NewIndex2 == 0xFFFF || NumNewVertices - NewIndex2 >= CONSTRAINED_CLUSTER_CACHE_SIZE )
				{
					NewIndex2 = NumNewVertices++;	NewToOldVertex[ NewIndex2 ] = OldIndex2;
				}
			} while( NumNewVertices > PrevNumVewVertices );

			// Output triangle
			OptimizedIndices[ NumNewTriangles * 3 + 0 ] = NewIndex0;
			OptimizedIndices[ NumNewTriangles * 3 + 1 ] = NewIndex1;
			OptimizedIndices[ NumNewTriangles * 3 + 2 ] = NewIndex2;
			NumNewTriangles++;

			// Disable selected triangle
			TrianglesEnabled[ NextTriangleIndex >> 5 ] &= ~( 1 << ( NextTriangleIndex & 31 ) );
		}
		RangeStart += RangeLength;
	}

	check( NumNewTriangles == NumOldTriangles );

	// Write back new triangle order
	for( uint32 i = 0; i < NumNewTriangles * 3; i++ )
	{
		Meshlet.Indexes[ i ] = OptimizedIndices[ i ];
	}

	// Write back new vertex order including possibly duplicates
	TArray<VertType> OldVertices = Meshlet.Verts;
	Meshlet.Verts.SetNumUninitialized( NumNewVertices );
	for( uint32 i = 0; i < NumNewVertices; i++ )
	{
		Meshlet.Verts[ i ] = OldVertices[ NewToOldVertex[ i ] ];
	}
	Cluster.NumVerts = NumNewVertices;
}

// Experimental alternative to ConstrainClusterFIFO based on geodesic distance. It tries to maximize reuse between material ranges by
// guiding triangle traversal order by geodesic distance to previous and next range triangles.
static void ConstrainClusterGeodesic( FTriCluster& Cluster, FMeshlet& Meshlet )
{
	uint32 NumOldTriangles = Cluster.NumTris;
	uint32 NumOldVertices = Cluster.NumVerts;

	const uint32 MAX_CLUSTER_TRIANGLES_IN_DWORDS = ( MAX_CLUSTER_TRIANGLES + 31 ) / 32;
	const uint32 MAX_DISTANCE = 0xFF;

	static_assert( MAX_CLUSTER_MATERIALS <= 64, "MAX_CLUSTER_MATERIALS is assumed to fit in uint64" );
	uint64 VertexRangesMask[ MAX_CLUSTER_TRIANGLES * 3 ] = { };
	uint8 VertexValences[ MAX_CLUSTER_TRIANGLES * 3 ] = {};

	// Calculate vertex valence and mark which ranges each vertex is in.
	const uint32 NumRanges = Cluster.MaterialRanges.Num();
	for( uint32 RangeIndex = 0; RangeIndex < NumRanges; RangeIndex++ )
	{
		const FMaterialRange& MaterialRange = Cluster.MaterialRanges[ RangeIndex ];
		for( uint32 i = 0; i < MaterialRange.RangeLength; i++ )
		{
			uint32 TriangleIndex = MaterialRange.RangeStart + i;
			uint32 OldIndex0 = Meshlet.Indexes[ TriangleIndex * 3 + 0 ];
			uint32 OldIndex1 = Meshlet.Indexes[ TriangleIndex * 3 + 1 ];
			uint32 OldIndex2 = Meshlet.Indexes[ TriangleIndex * 3 + 2 ];
			check( OldIndex1 != OldIndex0 && OldIndex2 != OldIndex0 && OldIndex2 != OldIndex1 );

			uint64 Mask = 1ull << RangeIndex;
			VertexRangesMask[ OldIndex0 ] |= Mask;
			VertexRangesMask[ OldIndex1 ] |= Mask;
			VertexRangesMask[ OldIndex2 ] |= Mask;
			VertexValences[ OldIndex0 ]++;
			VertexValences[ OldIndex1 ]++;
			VertexValences[ OldIndex2 ]++;
		}
	}

	uint16 OptimizedIndices[ MAX_CLUSTER_TRIANGLES * 3 ];

	uint32 NumNewVertices = 0;
	uint32 NumNewTriangles = 0;
	uint16 OldToNewVertex[ MAX_CLUSTER_TRIANGLES * 3 ];
	uint16 NewToOldVertex[ MAX_CLUSTER_TRIANGLES * 3 ];
	FMemory::Memset( OldToNewVertex, -1, sizeof( OldToNewVertex ) );

	uint16 ComponentStartScoreAndVertex[ MAX_CLUSTER_TRIANGLES ];	// (score << 9) | vertex
	FMemory::Memset( ComponentStartScoreAndVertex, -1, sizeof( ComponentStartScoreAndVertex ) );

	for( uint32 RangeIndex = 0; RangeIndex < NumRanges; RangeIndex++ )
	{
		const FMaterialRange& MaterialRange = Cluster.MaterialRanges[ RangeIndex ];
		uint32 RangeStart = MaterialRange.RangeStart;
		uint32 RangeLength = MaterialRange.RangeLength;

		uint8 VertexToComponent[ MAX_CLUSTER_TRIANGLES * 3 ];
		FMemory::Memset( VertexToComponent, -1, sizeof( VertexToComponent ) );

		// Associate every vertex with component ID by repeated relaxation. The component ID is the lowest triangle ID it is connected to.
		{
			bool bHasChanged;
			do 
			{
				bHasChanged = false;
				for( uint32 i = 0; i < RangeLength; i++ )
				{
					uint32 TriangleIndex = RangeStart + i;
					uint8& Component0 = VertexToComponent[ Meshlet.Indexes[ TriangleIndex * 3 + 0 ] ];
					uint8& Component1 = VertexToComponent[ Meshlet.Indexes[ TriangleIndex * 3 + 1 ] ];
					uint8& Component2 = VertexToComponent[ Meshlet.Indexes[ TriangleIndex * 3 + 2 ] ];

					uint32 MinTriangle = FMath::Min( TriangleIndex, (uint32)FMath::Min3( Component0, Component1, Component2 ) );
					if( MinTriangle < Component0 ) { Component0 = MinTriangle; bHasChanged = true; }
					if( MinTriangle < Component1 ) { Component1 = MinTriangle; bHasChanged = true; }
					if( MinTriangle < Component2 ) { Component2 = MinTriangle; bHasChanged = true; }
				}
			} while (bHasChanged);
		}

		bool bSeenComponent[ MAX_CLUSTER_TRIANGLES ] = { };
		uint32 NumSeenComponents = 0;

		// Score triangles and determine best scoring vertex for every component
		for( uint32 i = 0; i < RangeLength; i++ )
		{
			uint32 TriangleIndex = RangeStart + i;
			uint32 OldIndex0 = Meshlet.Indexes[ TriangleIndex * 3 + 0 ];
			uint32 OldIndex1 = Meshlet.Indexes[ TriangleIndex * 3 + 1 ];
			uint32 OldIndex2 = Meshlet.Indexes[ TriangleIndex * 3 + 2 ];

			uint32 Valence0 = VertexValences[ OldIndex0 ];
			uint32 Valence1 = VertexValences[ OldIndex1 ];
			uint32 Valence2 = VertexValences[ OldIndex2 ];

			uint32 Component = VertexToComponent[ OldIndex0 ];
			check( Component == VertexToComponent[ OldIndex1 ] && Component == VertexToComponent[ OldIndex2 ] );

			if( !bSeenComponent[ Component ] )
			{
				bSeenComponent[ Component ] = true;
				NumSeenComponents++;
			}

			uint32 Score = Valence0 + Valence1 + Valence2;
			uint32 StartVertex;
			if( Valence0 <= Valence1 && Valence0 <= Valence2 )
				StartVertex = OldIndex0;
			else if( Valence1 <= Valence0 && Valence1 <= Valence2 )
				StartVertex = OldIndex1;
			else
				StartVertex = OldIndex2;

			uint16 ScoreAndVertex = ( Score << 9 ) | StartVertex;
			ComponentStartScoreAndVertex[ Component ] = FMath::Min( ComponentStartScoreAndVertex[ Component ], ScoreAndVertex );
		}

		uint8 VertexDistances[ MAX_CLUSTER_TRIANGLES * 3 ][ 3 ];		// 0: Distance to previous range, 1: Distance to next range, 2: Distance to start triangle

																		// Mark material boundary vertices
		for( uint32 i = 0; i < RangeLength; i++ )
		{
			uint64 RangeBit = 1ull << RangeIndex;
			uint64 MaskLow = RangeBit - 1;
			uint64 MaskHigh = ~MaskLow ^ RangeBit;

			for( uint32 j = 0; j < 3; j++ )
			{
				uint32 OldIndex = Meshlet.Indexes[ (RangeStart + i) * 3 + j ];
				uint64 RangesMask = VertexRangesMask[ OldIndex ];
				uint32 Component = VertexToComponent[ OldIndex ];
				uint32 ComponentStartVertex = ComponentStartScoreAndVertex[ Component ] & 0x1FF;

				VertexDistances[ OldIndex ][ 0 ] = ( RangesMask & MaskLow ) ? 0 : MAX_DISTANCE;
				VertexDistances[ OldIndex ][ 1 ] = ( RangesMask & MaskHigh ) ? 0 : MAX_DISTANCE;
				VertexDistances[ OldIndex ][ 2 ] = OldIndex == ComponentStartVertex ? 0 : MAX_DISTANCE;
			}
		}

		// Relaxation to find minimum distance to next and previous range.
		bool bWasUpdated;
		do 
		{
			bWasUpdated = false;
			for( uint32 i = 0; i < RangeLength; i++ )
			{
				uint32 TriangleIndex = RangeStart + i;
				uint32 OldIndex0 = Meshlet.Indexes[ TriangleIndex * 3 + 0 ];
				uint32 OldIndex1 = Meshlet.Indexes[ TriangleIndex * 3 + 1 ];
				uint32 OldIndex2 = Meshlet.Indexes[ TriangleIndex * 3 + 2 ];

				for( uint32 j = 0; j < 3; j++ )
				{
					uint32 MinDist = FMath::Min3( VertexDistances[ OldIndex0 ][ j ], VertexDistances[ OldIndex1 ][ j ], VertexDistances[ OldIndex2 ][ j ] ) + 1;
					if( MinDist < VertexDistances[ OldIndex0 ][ j ] ) { VertexDistances[ OldIndex0 ][ j ] = MinDist; bWasUpdated = true; }
					if( MinDist < VertexDistances[ OldIndex1 ][ j ] ) { VertexDistances[ OldIndex1 ][ j ] = MinDist; bWasUpdated = true; }
					if( MinDist < VertexDistances[ OldIndex2 ][ j ] ) { VertexDistances[ OldIndex2 ][ j ] = MinDist; bWasUpdated = true; }
				}
			}
		} while (bWasUpdated);

		// Generate sort entries
		uint32 TriangleSortEntries[ MAX_CLUSTER_TRIANGLES ];
		for( uint32 i = 0; i < RangeLength; i++ )
		{
			uint32 TriangleIndex = RangeStart + i;

			uint32 OldIndex0 = Meshlet.Indexes[ TriangleIndex * 3 + 0 ];
			uint32 OldIndex1 = Meshlet.Indexes[ TriangleIndex * 3 + 1 ];
			uint32 OldIndex2 = Meshlet.Indexes[ TriangleIndex * 3 + 2 ];

			bool bConnectedToPrev = VertexDistances[ OldIndex0 ][ 0 ] != MAX_DISTANCE;
			bool bConnectedToNext = VertexDistances[ OldIndex0 ][ 1 ] != MAX_DISTANCE;

			bool bConnectedToPrev1 = VertexDistances[ OldIndex1 ][ 0 ] != MAX_DISTANCE;
			bool bConnectedToPrev2 = VertexDistances[ OldIndex2 ][ 0 ] != MAX_DISTANCE;
			bool bConnectedToNext1 = VertexDistances[ OldIndex1 ][ 1 ] != MAX_DISTANCE;
			bool bConnectedToNext2 = VertexDistances[ OldIndex2 ][ 1 ] != MAX_DISTANCE;

			check( bConnectedToPrev == bConnectedToPrev1 && bConnectedToPrev == bConnectedToPrev2 );
			check( bConnectedToNext == bConnectedToNext1 && bConnectedToNext == bConnectedToNext2 );

			uint32 Component = bConnectedToPrev ? 0 : bConnectedToNext ? ( MAX_CLUSTER_TRIANGLES + 1 ) : VertexToComponent[ OldIndex0 ] + 1;	// prev first, next last and everything else in the middle.

			uint32 Distance = 0x8000;
			if( bConnectedToPrev || bConnectedToNext )
			{
				// Connected to prev or next. Use distance from either or both for sorting
				Distance += VertexDistances[ OldIndex0 ][ 0 ] + VertexDistances[ OldIndex1 ][ 0 ] + VertexDistances[ OldIndex2 ][ 0 ];
				Distance -= VertexDistances[ OldIndex0 ][ 1 ] + VertexDistances[ OldIndex1 ][ 1 ] + VertexDistances[ OldIndex2 ][ 1 ];
			}
			else
			{
				// Independent component. Use distance from lowest valence vertex.
				Distance += VertexDistances[ OldIndex0 ][ 2 ] + VertexDistances[ OldIndex1 ][ 2 ] + VertexDistances[ OldIndex2 ][ 2 ];
			}
			TriangleSortEntries[ i ] = (Component << 24) | (Distance << 8) | TriangleIndex;

		}
		Sort( TriangleSortEntries, RangeLength );

		for(uint32 i = 0; i < RangeLength; i++)
		{
			uint32 TriangleIndex = TriangleSortEntries[ i ] & 0xFF;

			uint32 OldIndex0 = Meshlet.Indexes[ TriangleIndex * 3 + 0 ];
			uint32 OldIndex1 = Meshlet.Indexes[ TriangleIndex * 3 + 1 ];
			uint32 OldIndex2 = Meshlet.Indexes[ TriangleIndex * 3 + 2 ];

			uint16& NewIndex0 = OldToNewVertex[ OldIndex0 ];
			uint16& NewIndex1 = OldToNewVertex[ OldIndex1 ];
			uint16& NewIndex2 = OldToNewVertex[ OldIndex2 ];

			// Generate new indices such that they are all within a trailing window of size CONSTRAINED_CLUSTER_CACHE_SIZE of NumNewVertices.
			// This can require multiple iterations as new or duplicate vertices can push other
			uint32 PrevNumVewVertices;
			do
			{
				PrevNumVewVertices = NumNewVertices;
				if( NewIndex0 == 0xFFFF || NumNewVertices - NewIndex0 >= CONSTRAINED_CLUSTER_CACHE_SIZE ) {
					NewIndex0 = NumNewVertices++;	NewToOldVertex[ NewIndex0 ] = OldIndex0;
				}
				if( NewIndex1 == 0xFFFF || NumNewVertices - NewIndex1 >= CONSTRAINED_CLUSTER_CACHE_SIZE ) {
					NewIndex1 = NumNewVertices++;	NewToOldVertex[ NewIndex1 ] = OldIndex1;
				}
				if( NewIndex2 == 0xFFFF || NumNewVertices - NewIndex2 >= CONSTRAINED_CLUSTER_CACHE_SIZE ) {
					NewIndex2 = NumNewVertices++;	NewToOldVertex[ NewIndex2 ] = OldIndex2;
				}
			} while( NumNewVertices > PrevNumVewVertices );

			// Output triangle
			OptimizedIndices[ NumNewTriangles * 3 + 0 ] = NewIndex0;
			OptimizedIndices[ NumNewTriangles * 3 + 1 ] = NewIndex1;
			OptimizedIndices[ NumNewTriangles * 3 + 2 ] = NewIndex2;
			NumNewTriangles++;
		}
	}

	check( NumNewTriangles == NumOldTriangles );

	// Write back new triangle order
	for( uint32 i = 0; i < NumNewTriangles * 3; i++ )
	{
		Meshlet.Indexes[ i ] = OptimizedIndices[ i ];
	}

	// Write back new vertex order including possibly duplicates
	TArray<VertType> OldVertices = Meshlet.Verts;
	Meshlet.Verts.SetNumUninitialized( NumNewVertices );
	for( uint32 i = 0; i < NumNewVertices; i++ )
	{
		Meshlet.Verts[i] = OldVertices[ NewToOldVertex[ i ] ];
	}
	
	Cluster.NumVerts = NumNewVertices;
}

static void BuildClusterFromClusterTriangleRange( const FTriCluster& InCluster, const FMeshlet& InMeshlet, FTriCluster& OutCluster, FMeshlet& OutMeshlet, uint32 StartTriangle, uint32 NumTriangles )
{
	OutCluster = InCluster;
	OutCluster.MaterialRanges.Empty();

	OutMeshlet = InMeshlet;
	OutMeshlet.Indexes.Empty();
	OutMeshlet.MaterialIndexes.Empty();

	// Copy triangle indices and material indices.
	// Ignore that some of the vertices will no longer be referenced as that will be cleaned up in ConstrainCluster* pass
	OutMeshlet.Indexes.SetNumUninitialized( NumTriangles * 3 );
	OutMeshlet.MaterialIndexes.SetNumUninitialized( NumTriangles );
	for( uint32 i = 0; i < NumTriangles; i++ )
	{
		uint32 TriangleIndex = StartTriangle + i;
			
		OutMeshlet.MaterialIndexes[ i ] = InMeshlet.MaterialIndexes[ TriangleIndex ];
		OutMeshlet.Indexes[ i * 3 + 0 ] = InMeshlet.Indexes[ TriangleIndex * 3 + 0 ];
		OutMeshlet.Indexes[ i * 3 + 1 ] = InMeshlet.Indexes[ TriangleIndex * 3 + 1 ];
		OutMeshlet.Indexes[ i * 3 + 2 ] = InMeshlet.Indexes[ TriangleIndex * 3 + 2 ];
	}

	OutCluster.NumTris = NumTriangles;

	// Rebuild material range and reconstrain 
	BuildMaterialRanges( OutCluster, OutMeshlet );
	ConstrainClusterFIFO( OutCluster, OutMeshlet );
}

#if 0
// Dump Cluster to .obj for debugging
static void DumpClusterToObj( const char* Filename,  const FTriCluster& Cluster, const FMeshlet& Meshlet)
{
	FILE* File = nullptr;
	fopen_s( &File, Filename, "wb" );

	for( const VertType& Vert : Meshlet.Verts )
	{
		fprintf( File, "v %f %f %f\n", Vert.Position.X, Vert.Position.Y, Vert.Position.Z );
	}

	uint32 NumRanges = Cluster.MaterialRanges.Num();
	uint32 NumTriangles = Meshlet.Indexes.Num() / 3;
	for( uint32 RangeIndex = 0; RangeIndex < NumRanges; RangeIndex++ )
	{
		const FMaterialRange& MaterialRange = Cluster.MaterialRanges[ RangeIndex ];
		fprintf( File, "newmtl range%d\n", RangeIndex );
		float r = ( RangeIndex + 0.5f ) / NumRanges;
		fprintf( File, "Kd %f %f %f\n", r, 0.0f, 0.0f );
		fprintf( File, "Ks 0.0, 0.0, 0.0\n" );
		fprintf( File, "Ns 18.0\n" );
		fprintf( File, "usemtl range%d\n", RangeIndex );
		for( uint32 i = 0; i < MaterialRange.RangeLength; i++ )
		{
			uint32 TriangleIndex = MaterialRange.RangeStart + i;
			fprintf( File, "f %d %d %d\n", Meshlet.Indexes[ TriangleIndex * 3 + 0 ] + 1, Meshlet.Indexes[ TriangleIndex * 3 + 1 ] + 1, Meshlet.Indexes[ TriangleIndex * 3 + 2 ] + 1 );
		}
	}

	fclose( File );
}
#endif

static void ConstrainClusters( TArray< FClusterGroup >& ClusterGroups, TArray< FTriCluster >& Clusters, TArray< FMeshlet >& Meshlets )
{
	// Calculate stats
	uint32 TotalOldTriangles = 0;
	uint32 TotalOldVertices = 0;
	for( const FTriCluster& Cluster : Clusters )
	{
		TotalOldTriangles += Cluster.NumTris;
		TotalOldVertices += Cluster.NumVerts;
	}

	ParallelFor( Clusters.Num(),
		[&]( uint32 i )
		{
			ConstrainClusterFIFO( Clusters[i], Meshlets[i] );
		} );
	
	uint32 TotalNewTriangles = 0;
	uint32 TotalNewVertices = 0;

	// Constrain clusters
	const uint32 NumOldClusters = Clusters.Num();
	for( uint32 i = 0; i < NumOldClusters; i++ )
	{
		TotalNewTriangles += Clusters[ i ].NumTris;
		TotalNewVertices += Clusters[ i ].NumVerts;
		
		// Split clusters with too many verts
		if( Clusters[ i ].NumVerts > 256 )
		{
			FTriCluster ClusterA, ClusterB;
			FMeshlet MeshletA, MeshletB;
			uint32 NumTrianglesA = Clusters[ i ].NumTris / 2;
			uint32 NumTrianglesB = Clusters[ i ].NumTris - NumTrianglesA;
			BuildClusterFromClusterTriangleRange( Clusters[ i ], Meshlets[ i ], ClusterA, MeshletA, 0, NumTrianglesA );
			BuildClusterFromClusterTriangleRange( Clusters[ i ], Meshlets[ i ], ClusterB, MeshletB, NumTrianglesA, NumTrianglesB );
			Clusters[ i ] = ClusterA;
			Meshlets[ i ] = MeshletA;
			ClusterGroups[ ClusterB.ClusterGroupIndex ].Children.Add( Clusters.Num() );
			Clusters.Add( ClusterB );
			Meshlets.Add( MeshletB );
		}
	}

	// Calculate stats
	uint32 TotalNewTrianglesWithSplits = 0;
	uint32 TotalNewVerticesWithSplits = 0;
	for( const FTriCluster& Cluster : Clusters )
	{
		TotalNewTrianglesWithSplits += Cluster.NumTris;
		TotalNewVerticesWithSplits += Cluster.NumVerts;
	}

	UE_LOG( LogStaticMesh, Log, TEXT("ConstrainClusters:") );
	UE_LOG( LogStaticMesh, Log, TEXT("  Input: %d Clusters, %d Triangles and %d Vertices"), NumOldClusters, TotalOldTriangles, TotalOldVertices );
	UE_LOG( LogStaticMesh, Log, TEXT("  Output without splits: %d Clusters, %d Triangles and %d Vertices"), NumOldClusters, TotalNewTriangles, TotalNewVertices );
	UE_LOG( LogStaticMesh, Log, TEXT("  Output with splits: %d Clusters, %d Triangles and %d Vertices"), Clusters.Num(), TotalNewTrianglesWithSplits, TotalNewVerticesWithSplits );
}

#if DO_CHECK
static void VerifyClusterContraints( const TArray< FTriCluster >& Clusters, const TArray< FMeshlet >& Meshlets )
{
	ParallelFor( Clusters.Num(),
		[&]( uint32 i )
		{
			VerifyClusterConstaints( Clusters[i], Meshlets[i] );
		} );
}
#endif

static uint32 BuildCoarseRepresentation(
	const FMeshlet& CoarseRepresentation,
	TArray< FStaticMeshSection, TInlineAllocator<1> >& Sections,
	TArray<uint32>& Indices,
	TArray<FStaticMeshBuildVertex>& Vertices,
	uint32& NumTexCoords)
{
	TArray< FStaticMeshSection, TInlineAllocator<1> > OldSections = Sections;

	// Need to update coarse representation UV count to match new data.
	NumTexCoords = sizeof(VertType::UVs) / sizeof(FVector2D);

	// Rebuild vertex data
	Vertices.Empty( CoarseRepresentation.Verts.Num() );
	for (const auto& CoarseVert : CoarseRepresentation.Verts)
	{
		FStaticMeshBuildVertex Vertex;
		Vertex.Position = CoarseVert.GetPos();
		Vertex.TangentX = FVector::ZeroVector; // TODO: Should probably have correct TSB for non-Nanite rendering fallback
		Vertex.TangentY = FVector::ZeroVector; // TODO: Should probably have correct TSB for non-Nanite rendering fallback
		Vertex.TangentZ = CoarseVert.Normal;
		for (uint32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++)
		{
			Vertex.UVs[UVIndex] = CoarseVert.UVs[UVIndex].ContainsNaN() ? FVector2D::ZeroVector : CoarseVert.UVs[UVIndex];
		}
		Vertex.Color = CoarseVert.Color.ToFColor(false /* sRGB */);
		Vertices.Add(Vertex);
	}

	TArray<FMaterialTriangle, TInlineAllocator<128>> CoarseMaterialTris;
	TArray<FMaterialRange, TInlineAllocator<4>> CoarseMaterialRanges;

	// Compute material ranges for coarse representation.
	BuildMaterialRanges(
		CoarseRepresentation.Indexes,
		CoarseRepresentation.MaterialIndexes,
		CoarseMaterialTris,
		CoarseMaterialRanges);

	// Rebuild section data.
	Sections.Reset(CoarseMaterialRanges.Num());
	for (const FMaterialRange& Range : CoarseMaterialRanges)
	{
		FStaticMeshSection Section;

		// The index of the material with which to render this section.
		Section.MaterialIndex = Range.MaterialIndex;

		// Range of vertices and indices used when rendering this section.
		Section.FirstIndex = Range.RangeStart;
		Section.NumTriangles = Range.RangeLength;
		Section.MinVertexIndex = TNumericLimits<uint32>::Max();
		Section.MaxVertexIndex = TNumericLimits<uint32>::Min();

		for (uint32 TriangleIndex = 0; TriangleIndex < (Range.RangeStart + Range.RangeLength); ++TriangleIndex)
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

		// Copy properties from original mesh sections.
		int32 OldSectionIndex = INDEX_NONE;
		for (int32 SectionIndex = 0; SectionIndex < OldSections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& OldSection = OldSections[SectionIndex];
			if (OldSection.MaterialIndex == Section.MaterialIndex)
			{
				Section.bCastShadow = OldSection.bCastShadow;
				Section.bEnableCollision = OldSection.bEnableCollision;
			#if WITH_EDITORONLY_DATA
				for (int32 TexCoord = 0; TexCoord < MAX_STATIC_TEXCOORDS; ++TexCoord)
				{
					Section.UVDensities[TexCoord] = OldSection.UVDensities[TexCoord];
					Section.Weights[TexCoord] = OldSection.Weights[TexCoord];
				}
			#endif
				OldSectionIndex = SectionIndex;
				break;
			}
		}

		// Make sure we found a matching original mesh section
		check(OldSectionIndex != INDEX_NONE);
		Sections.Add(Section);
	}

	// Rebuild index data.
	Indices.Reset();
	for (const FMaterialTriangle& Triangle : CoarseMaterialTris)
	{
		Indices.Add(Triangle.Index0);
		Indices.Add(Triangle.Index1);
		Indices.Add(Triangle.Index2);
	}

	return CoarseMaterialTris.Num();
}

static bool BuildNaniteData(
	FResources& Resources,
	TArray<FStaticMeshSection, TInlineAllocator<1>>& CoarseSections,
	TArray<FStaticMeshBuildVertex>& Verts, // TODO: Do not require this vertex type for all users of Nanite
	TArray<uint32>& Indexes,
	TArray<int32>&  MaterialIndexes,
	uint32& NumTexCoords,
	bool& bHasColors,
	const FMeshNaniteSettings& Settings
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::BuildData"));

	uint32 NumTriangles = Indexes.Num() / 3;

	uint32 Time0 = FPlatformTime::Cycles();

	//UE_LOG(LogStaticMesh, Log, TEXT("Source Vertices CRC %u"), FCrc::MemCrc32( Vertices.GetData(), Vertices.Num() * Vertices.GetTypeSize()));
	//UE_LOG(LogStaticMesh, Log, TEXT("Source Indices CRC %u"), FCrc::MemCrc32( TriangleIndices.GetData(), Indexes.Num() * TriangleIndices.GetTypeSize()));

	uint32 BoundaryTime = Time0;

	FBounds	MeshBounds;
	
	// Normalize UVWeights using min/max UV range.
	float MinUV[ MAX_STATIC_TEXCOORDS ] = { +FLT_MAX, +FLT_MAX };
	float MaxUV[ MAX_STATIC_TEXCOORDS ] = { -FLT_MAX, -FLT_MAX };
	for( auto& Vert : Verts )
	{
		MeshBounds += Vert.Position;

		for( uint32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
		{
			MinUV[ UVIndex ] = FMath::Min( MinUV[ UVIndex ], Vert.UVs[ UVIndex ].X );
			MinUV[ UVIndex ] = FMath::Min( MinUV[ UVIndex ], Vert.UVs[ UVIndex ].Y );
			MaxUV[ UVIndex ] = FMath::Max( MaxUV[ UVIndex ], Vert.UVs[ UVIndex ].X );
			MaxUV[ UVIndex ] = FMath::Max( MaxUV[ UVIndex ], Vert.UVs[ UVIndex ].Y );
		}
	}

	float UVWeights[ MAX_STATIC_TEXCOORDS ] = { 0.0f };
	for( uint32 UVIndex = 0; UVIndex < NumTexCoords; UVIndex++ )
	{
		UVWeights[ UVIndex ] = 1.0f / ( 32.0f * NumTexCoords * FMath::Max( 1.0f, MaxUV[ UVIndex ] - MinUV[ UVIndex ] ) );
	}

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
		} );

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
		} );

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

	BoundaryTime = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Boundary [%.2fs], verts: %i, tris: %i"), FPlatformTime::ToMilliseconds( BoundaryTime - Time0 ) / 1000.0f, Verts.Num(), Indexes.Num() / 3 );
	//UE_LOG( LogStaticMesh, Log, TEXT("Boundary CRC %u"), FCrc::MemCrc32( BoundaryEdges.GetData(), BoundaryEdges.GetAllocatedSize() ) );
	//UE_LOG( LogStaticMesh, Log, TEXT("SharedEdges CRC %u"), FCrc::MemCrc32( SharedEdges.GetData(), SharedEdges.Num() * SharedEdges.GetTypeSize() ) );

	FMeshlet CoarseRepresentation;

	FGraphPartitioner Partitioner( NumTriangles );

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build::PartitionGraph"));

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

		Partitioner.PartitionStrict( Graph, FMeshlet::ClusterSize - 4, FMeshlet::ClusterSize, true );
	}

	const uint32 OptimalNumClusters = FMath::DivideAndRoundUp< int32 >( Indexes.Num(), FMeshlet::ClusterSize * 3 );

	uint32 ClusterTime = FPlatformTime::Cycles();
	UE_LOG( LogStaticMesh, Log, TEXT("Clustering [%.2fs]. Ratio: %f"), FPlatformTime::ToMilliseconds( ClusterTime - BoundaryTime ) / 1000.0f, (float)Partitioner.Ranges.Num() / OptimalNumClusters );

	TArray< FMeshlet >		Meshlets;
	TArray< FTriCluster >	Clusters;

	if( Partitioner.Ranges.Num() )
	{
		//UE_LOG( LogStaticMesh, Log, TEXT("TriIndexes CRC %u"), FCrc::MemCrc32( Partitioner.Indexes.GetData(), Partitioner.Indexes.Num() * Partitioner.Indexes.GetTypeSize() ) );
		//UE_LOG( LogStaticMesh, Log, TEXT("ClusterRanges CRC %u"), FCrc::MemCrc32( Partitioner.Ranges.GetData(), Partitioner.Ranges.Num() * Partitioner.Ranges.GetTypeSize() ) );

		Meshlets.AddDefaulted( Partitioner.Ranges.Num() );
		Clusters.AddDefaulted( Partitioner.Ranges.Num() );

		const bool bSingleThreaded = false;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build::BuildClusters"));
			ParallelFor( Partitioner.Ranges.Num(),
				[&]( int32 Index )
				{
					auto& Range = Partitioner.Ranges[ Index ];

					Meshlets[ Index ] = FMeshlet( Verts, Indexes, MaterialIndexes, BoundaryEdges, Range.Begin, Range.End, Partitioner.Indexes );
					Clusters[ Index ] = BuildCluster( Meshlets[ Index ] );

					// Negative notes it's a leaf
					Clusters[ Index ].EdgeLength *= -1.0f;
				}, bSingleThreaded);
		}

		uint32 LeavesTime = FPlatformTime::Cycles();
		UE_LOG( LogStaticMesh, Log, TEXT("Leaves [%.2fs]"), FPlatformTime::ToMilliseconds( LeavesTime - ClusterTime ) / 1000.0f );

		TArray< FClusterGroup > Groups;
		FMeshletDAG DAG( Meshlets, Clusters, Groups, UVWeights, CoarseRepresentation);
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build::DAG.Reduce"));
			DAG.Reduce( Settings );
		}

		uint32 ReduceTime = FPlatformTime::Cycles();
		UE_LOG(LogStaticMesh, Log, TEXT("Reduce [%.2fs]"), FPlatformTime::ToMilliseconds(ReduceTime - LeavesTime) / 1000.0f);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT( TEXT("Nanite::Build::BuildMaterialRanges") );
			BuildMaterialRanges( Clusters, Meshlets );
		}


#if USE_CONSTRAINED_CLUSTERS
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT( TEXT( "Nanite::Build::ConstrainClusters" ) );
			ConstrainClusters( Groups, Clusters, Meshlets );
		}
#if DO_CHECK
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT( TEXT( "Nanite::Build::VerifyClusterConstraints" ) );
			VerifyClusterContraints( Clusters, Meshlets );
		}
#endif
#endif

		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build::CalculateQuantizedPositions"));	
			CalculateQuantizedPositions(Clusters, Meshlets, DAG.MeshBounds);	// Needs to happen after clusters have been constrained and split.
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build::PrintMaterialRangeStats"));
			PrintMaterialRangeStats(Clusters, Meshlets);
		}

		TArray<FPage> Pages;
		TArray<FClusterGroupPart> GroupParts;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build::EncodeClustersAndAssignToPages"));
			EncodeClustersAndAssignToPages(Resources, Groups, Clusters, Meshlets, NumTexCoords, Pages, GroupParts);
		}

		TArray< Nanite::FHierarchyNode > HierarchyNodes;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT( TEXT( "Nanite::Build::BuildHierarchyNodes" ) );
			BuildHierarchyNodesKMeans(HierarchyNodes, Groups, GroupParts);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build::BuildPages"));
			WritePages(Resources, Pages, Groups, GroupParts, Clusters);
		}
		
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build::PackHierarchyNodes"));
			const uint32 NumHierarchyNodes = HierarchyNodes.Num();
			Resources.HierarchyNodes.AddUninitialized(NumHierarchyNodes);
			for (uint32 i = 0; i < NumHierarchyNodes; i++)
			{
				PackHierarchyNode( Resources.HierarchyNodes[i], HierarchyNodes[i], Groups, GroupParts );
			}
		}

		uint32 EncodeTime = FPlatformTime::Cycles();
		UE_LOG( LogStaticMesh, Log, TEXT("Encode [%.2fs]"), FPlatformTime::ToMilliseconds( EncodeTime - ReduceTime ) / 1000.0f );
	}

	// Replace original static mesh data with coarse representation.
	{
		const uint32 OldTriangleCount = Indexes.Num() / 3;
		uint32 CoarseTriangleCount = OldTriangleCount;
		const uint32 CoarseStartTime = FPlatformTime::Cycles();
		const bool bUseCoarseRepresentation = Settings.PercentTriangles < 1.0f;
		if (bUseCoarseRepresentation)
		{
			CoarseTriangleCount = BuildCoarseRepresentation(CoarseRepresentation, CoarseSections, Indexes, Verts, NumTexCoords);
		}
		const uint32 CoarseEndTime = FPlatformTime::Cycles();
		UE_LOG(LogStaticMesh, Log, TEXT("Coarse [%.2fs], original tris: %d, coarse tris: %d"), FPlatformTime::ToMilliseconds(CoarseEndTime - CoarseStartTime) / 1000.0f, OldTriangleCount, CoarseTriangleCount);
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
	uint32& NumTexCoords,
	bool& bHasColors,
	const FMeshNaniteSettings& Settings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build"));

	check(Settings.PercentTriangles == 1.0f); // No coarse representation used by this path
	TArray<FStaticMeshSection, TInlineAllocator<1>> IgnoredCoarseSections;
	return BuildNaniteData(
		Resources,
		IgnoredCoarseSections,
		Vertices,
		TriangleIndices,
		MaterialIndices,
		NumTexCoords,
		bHasColors,
		Settings
	);
}

bool FBuilderModule::Build(
	FResources& Resources,
	TArray< FStaticMeshBuildVertex>& Vertices,
	TArray< uint32 >& TriangleIndices,
	TArray< FStaticMeshSection, TInlineAllocator<1>>& Sections,
	uint32& NumTexCoords,
	bool& bHasColors,
	const FMeshNaniteSettings& Settings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::Build"));

	// TODO: Properly error out if # of unique materials is > 64 (error message to editor log)
	check(Sections.Num() > 0 && Sections.Num() <= 64);

	// Build associated array of triangle index and material index.
	TArray<int32> MaterialIndices;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("Nanite::BuildSections"));
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

	// Make sure there is 1 material index per triangle.
	check(MaterialIndices.Num() * 3 == TriangleIndices.Num());

	return BuildNaniteData(
		Resources,
		Sections,
		Vertices,
		TriangleIndices,
		MaterialIndices,
		NumTexCoords,
		bHasColors,
		Settings
	);
}

} // namespace Nanite