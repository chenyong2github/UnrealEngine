// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "RHIDefinitions.h"
#include "PackedNormal.h"
#include "HairStrandsVertexFactory.h"

////////////////////////////////////////////////////////////////////////////////////////////////
// Utils/Format data

struct FUIntPoint
{
	uint32 X;
	uint32 Y;
};

struct FHairCardsPositionFormat
{
	typedef FVector4 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
};

struct FHairCardsStrandsAttributeFormat
{
	typedef FVector4 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
};

struct FHairCardsUVFormat
{
	// Store atlas UV and (approximated) root UV
	typedef FVector4 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F; // TODO
};

struct FHairCardsNormalFormat
{
	typedef FPackedNormal Type;

	// TangentX & tangentZ are packed into 2 * PF_R8G8B8A8_SNORM
	static const uint32 ComponentCount = 2;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_PackedNormal;
	static const EPixelFormat Format = PF_R8G8B8A8_SNORM;
};

struct FHairCardsIndexFormat
{
	typedef uint32 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UInt;
	static const EPixelFormat Format = PF_R32_UINT;
};

struct FHairCardsAtlasRectFormat
{
	struct Type { uint16_t X; uint16_t Y; uint16_t Z; uint16_t W; };
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UShort4;
	static const EPixelFormat Format = PF_R16G16B16A16_UINT;
};

struct FHairCardsDimensionFormat
{
	typedef float Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float1;
	static const EPixelFormat Format = PF_R32_FLOAT;
};

struct FHairCardsStrandsPositionFormat
{
	typedef FVector4 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
}; 

struct FHairCardsOffsetAndCount
{
	typedef FUIntPoint Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_None;
	static const EPixelFormat Format = PF_R32G32_UINT;
};

struct FHairCardsBoundsFormat
{
	typedef FVector4 Type;
	static const uint32 ComponentCount = 4;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
};

struct FHairCardsVoxelDensityFormat
{
	typedef uint32 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UInt;
	static const EPixelFormat Format = PF_R32_UINT;
};

struct FHairCardsVoxelTangentFormat
{
	typedef FVector4 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
};

struct FHairCardsInterpolationVertex
{
	uint32 VertexIndex : 24;
	uint32 VertexLerp  : 8;
};

FArchive& operator<<(FArchive& Ar, FHairCardsInterpolationVertex& Vertex);

struct FHairCardsInterpolationFormat
{
	typedef FHairCardsInterpolationVertex Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UInt;
	static const EPixelFormat Format = PF_R32_UINT;
};

struct FHairOrientedBound
{
	FVector Center;
	FVector ExtentX;
	FVector ExtentY;
	FVector ExtentZ;

	FVector& Extent(uint8 Axis)
	{
		if (Axis == 0) return ExtentX;
		if (Axis == 1) return ExtentY;
		if (Axis == 2) return ExtentZ;
		return ExtentX;
	}

	const FVector& Extent(uint8 Axis) const
	{
		if (Axis == 0) return ExtentX;
		if (Axis == 1) return ExtentY;
		if (Axis == 2) return ExtentZ;
		return ExtentX;
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////
// Hair Cards

// Data structure holding cards geometry information
struct FHairCardsGeometry
{
	// Geometry
	TArray<FVector4> UVs;
	TArray<FVector>  Normals;
	TArray<FVector>  Tangents;
	TArray<FVector>  Positions;
	TArray<uint32>   Indices;
	TArray<float>    CoordU; // Transient data storing [0..1] parametric value along main axis. This is used for generating guides & interpolation data

	// Vertex offset and vertex count of each cards geometry
	// No longer used, kept only for backward compatibility
	TArray<uint32> PointOffsets;
	TArray<uint32> PointCounts;

	// Index offset and index count of each cards geometry
	TArray<uint32> IndexOffsets;
	TArray<uint32> IndexCounts;

	FBox BoundingBox;

	void Reset()
	{
		UVs.Reset();
		Normals.Reset();
		Tangents.Reset();
		Positions.Reset();
		Indices.Reset();
		CoordU.Reset();

		IndexOffsets.Reset();
		IndexCounts.Reset();

		BoundingBox.Init();
	}

	void SetNum(uint32 Count)
	{
		// Geometry
		UVs.Empty();
		Normals.Empty();
		Tangents.Empty();
		Positions.Empty();
		Indices.Empty();
		CoordU.Reset();

		// Cards' indices offset & count
		IndexOffsets.SetNum(Count);
		IndexCounts.SetNum(Count);
	}

	uint32 GetNumTriangles() const
	{
		return Indices.Num() / 3;
	}

	uint32 GetNumVertices() const
	{
		return Positions.Num();
	}

	uint32 GetNumCards() const
	{
		return IndexOffsets.Num();
	}
};

struct FHairCardsDatas
{
	bool IsValid() const { return RenderData.Positions.Num() > 0; }

	FHairCardsGeometry Cards;

	UTexture2D* DepthTexture = nullptr;
	UTexture2D* TangentTexture = nullptr;
	UTexture2D* CoverageTexture = nullptr;
	UTexture2D* AttributeTexture = nullptr;

	struct FRenderData
	{
		TArray<FHairCardsPositionFormat::Type> Positions;
		TArray<FHairCardsNormalFormat::Type> Normals;
		TArray<FHairCardsUVFormat::Type> UVs;
		TArray<FHairCardsIndexFormat::Type> Indices;
	} RenderData;
};

FArchive& operator<<(FArchive& Ar, FHairCardsDatas& CardData);

////////////////////////////////////////////////////////////////////////////////////////////////
// Hair Cards (Procedural generation)

// Internal data structure for computing surface flow. 
// This structured is exposed only for debug purpose
struct FHairCardsVoxel
{
	FRDGExternalBuffer TangentBuffer;
	FRDGExternalBuffer NormalBuffer;
	FRDGExternalBuffer DensityBuffer;
	FIntVector	Resolution;
	FVector		MinBound;
	FVector		MaxBound;
	float		VoxelSize;
};

struct FHairCardsProceduralGeometry : FHairCardsGeometry
{
	struct Rect
	{
		FIntPoint Offset = FIntPoint(0, 0);
		FIntPoint Resolution = FIntPoint(0, 0);
	};

	TArray<uint32> CardIndices; // Store the cards index whose a verex belongs to

	TArray<Rect>   Rects;
	TArray<float>  Lengths;
	
	TArray<FUIntPoint> CardIndexToClusterOffsetAndCount;
	TArray<FUIntPoint> ClusterIndexToVertexOffsetAndCount;

	TArray<FHairOrientedBound> Bounds;

	void SetNum(uint32 Count)
	{
		FHairCardsGeometry::SetNum(Count);
		CardIndices.Empty();

		Rects.SetNum(Count);
		Lengths.SetNum(Count);
		Bounds.SetNum(Count);

		// Cluster infos (editor only, for texture generation)
		CardIndexToClusterOffsetAndCount.Empty();
		ClusterIndexToVertexOffsetAndCount.Empty();
	}

	uint32 GetNum() const
	{
		return Bounds.Num();
	}
};

struct FHairCardsProceduralAtlas
{
	struct Rect
	{
		FIntPoint Offset = FIntPoint(0, 0);
		FIntPoint Resolution = FIntPoint(0, 0);
		uint32 VertexOffset = 0;
		uint32 VertexCount = 0;
		FVector MinBound = FVector::ZeroVector;
		FVector MaxBound = FVector::ZeroVector;

		FVector RasterAxisX = FVector::ZeroVector;
		FVector RasterAxisY = FVector::ZeroVector;
		FVector RasterAxisZ = FVector::ZeroVector;

		float CardWidth = 0;
		float CardLength = 0;
	};

	FIntPoint Resolution;
	TArray<Rect> Rects;
	TArray<FVector4> StrandsPositions;
	TArray<FVector4> StrandsAttributes;
	bool bIsDirty = true;
};


// Data structure holding cards geometry information and 
// intermediate data used for generating cards data based 
// on strands groom
struct FHairCardsProceduralDatas
{
	FHairStrandsDatas Guides;
	FHairCardsProceduralGeometry Cards;
	FHairCardsProceduralAtlas Atlas;
	FHairCardsVoxel Voxels;

	struct FRenderData
	{
		TArray<FHairCardsPositionFormat::Type> Positions;
		TArray<FHairCardsNormalFormat::Type> Normals;
		TArray<FHairCardsUVFormat::Type> UVs;
		TArray<FHairCardsIndexFormat::Type> Indices;
		TArray<FHairCardsAtlasRectFormat::Type> CardsRect;
		TArray<FHairCardsDimensionFormat::Type> CardsLengths;
		TArray<FHairCardsStrandsPositionFormat::Type> CardsStrandsPositions;
		TArray<FHairCardsStrandsAttributeFormat::Type> CardsStrandsAttributes;
		
		TArray<FHairCardsOffsetAndCount::Type> CardItToCluster;		// Offset & Count
		TArray<FHairCardsOffsetAndCount::Type> ClusterIdToVertices; // Offset & count
		TArray<FHairCardsBoundsFormat::Type> ClusterBounds;

		TArray<FHairCardsVoxelDensityFormat::Type> VoxelDensity;
		TArray<FHairCardsVoxelTangentFormat::Type> VoxelTangent;
		TArray<FHairCardsVoxelTangentFormat::Type> VoxelNormal;
	} RenderData;
};

struct HAIRSTRANDSCORE_API FHairCardsSourceData
{
	FHairCardsDatas				ImportedData;
	FHairCardsProceduralDatas	ProceduralData;
};

////////////////////////////////////////////////////////////////////////////////////////////////
// Hair meshes

struct FHairMeshes
{
	// Geometry
	TArray<FVector2D> UVs;
	TArray<FVector>   Normals;
	TArray<FVector>   Tangents;
	TArray<FVector>   Positions;
	TArray<uint32>    Indices;

	FBox BoundingBox;

	void SetNum(uint32 Count)
	{
		// Geometry
		UVs.Empty();
		Normals.Empty();
		Tangents.Empty();
		Positions.Empty();
		Indices.Empty();

		BoundingBox.Init();
	}

	uint32 GetNumTriangles() const
	{
		return Indices.Num() / 3;
	}

	uint32 GetNumVertices() const
	{
		return Positions.Num();
	}
};

struct FHairMeshesDatas
{
	bool IsValid() const { return RenderData.Positions.Num() > 0; }

	FHairMeshes Meshes;

	struct FRenderData
	{
		TArray<FHairCardsPositionFormat::Type> Positions;
		TArray<FHairCardsNormalFormat::Type> Normals;
		TArray<FHairCardsUVFormat::Type> UVs;
		TArray<FHairCardsIndexFormat::Type> Indices;		
	} RenderData;
};

FArchive& operator<<(FArchive& Ar, FHairMeshesDatas& MeshData);
