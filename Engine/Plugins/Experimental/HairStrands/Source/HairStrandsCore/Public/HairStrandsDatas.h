// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "RHIDefinitions.h"
#include "PackedNormal.h"
#include "HairStrandsVertexFactory.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairStrands, Log, All);

struct FPackedHairVertex
{
	FFloat16 X, Y, Z;
	uint8 ControlPointType : 2;
	uint8 NormalizedRadius : 6;
	uint8 NormalizedLength;
};

struct FPackedHairAttributeVertex
{
	uint8 RootU;
	uint8 RootV;
	uint8 UCoord;
	uint8 Seed;

	uint8 IndexU;
	uint8 IndexV;
	uint8 Unused0;
	uint8 Unused1;
};

struct FHairMaterialVertex
{
	// sRGB color space
	uint8 BaseColorR;
	uint8 BaseColorG;
	uint8 BaseColorB;

	uint8 Roughness;
};

struct FHairInterpolation0Vertex
{
	uint16 Index0;
	uint16 Index1;
	uint16 Index2;

	uint8 VertexWeight0;
	uint8 VertexWeight1;
};

struct FHairInterpolation1Vertex
{
	uint8 VertexIndex0;
	uint8 VertexIndex1;
	uint8 VertexIndex2;

	uint8 VertexLerp0;
	uint8 VertexLerp1;
	uint8 VertexLerp2;

	uint8 Pad0;
	uint8 Pad1;
};

struct FVector4_16
{
	FFloat16 X;
	FFloat16 Y;
	FFloat16 Z;
	FFloat16 W;
}; 

struct FHairStrandsPositionFormat
{
	typedef FPackedHairVertex Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UShort4;
	static const EPixelFormat Format = PF_R16G16B16A16_UINT;
};

struct FHairStrandsAttributeFormat
{
	typedef FPackedHairAttributeVertex Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UShort4;
	static const EPixelFormat Format = PF_R16G16B16A16_UINT;
};

struct FHairStrandsMaterialFormat
{
	typedef FHairMaterialVertex Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UByte4;
	static const EPixelFormat Format = PF_R8G8B8A8;
};

struct FHairStrandsTangentFormat
{
	typedef FPackedNormal Type;

	// TangentX & tangentZ are packed into 2 * PF_R8G8B8A8_SNORM
	static const uint32 ComponentCount = 2;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_R8G8B8A8_SNORM;
};

struct FHairStrandsInterpolation0Format
{
	typedef FHairInterpolation0Vertex Type;

	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UShort4;
	static const EPixelFormat Format = PF_R16G16B16A16_UINT;
};

struct FHairStrandsInterpolation1Format
{
	typedef FHairInterpolation1Vertex Type;

	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UShort4;
	static const EPixelFormat Format = PF_R16G16B16A16_UINT;
};

struct FHairStrandsRootIndexFormat
{
	typedef uint32 Type;

	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UInt;
	static const EPixelFormat Format = PF_R32_UINT;
};

struct FHairStrandsRaytracingFormat
{
	typedef FVector4 Type;

	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
};

/** Hair strands index format */
struct HAIRSTRANDSCORE_API FHairStrandsIndexFormat
{
	using Type = uint32;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UInt;
	static const EPixelFormat Format = PF_R32_UINT;
};

/** Hair strands weights format */
struct FHairStrandsWeightFormat
{
	using Type = float;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float1;
	static const EPixelFormat Format = PF_R32_FLOAT;
};

/** 
 * Skinned mesh triangle vertex position format
 * Two precision options are available: 4x16bits or 4x32bits
 * Triangle vertices are relative to their bounding box in order to preserve precision, 
 * however this is sometime not enough for large asset, this is why by default the format 
 * use 32bits precision
*/
struct FHairStrandsMeshTrianglePositionFormat
{
#if 1
	using Type = FVector4;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
#else
	using Type = FVector4_16;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_FloatRGBA;
#endif
};

struct FHairStrandsCurveTriangleIndexFormat
{
	using Type = uint32;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UInt;
	static const EPixelFormat Format = PF_R32_UINT;
};

struct FHairStrandsCurveTriangleBarycentricFormat
{
	using Type = uint32;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UInt;
	static const EPixelFormat Format = PF_R32_UINT;
};

struct FHairStrandsRootPositionFormat
{
	typedef FVector4 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
};

struct FHairStrandsRootNormalFormat
{
	typedef FVector4_16 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_FloatRGBA;
};

/** Hair strands points interpolation attributes */
struct HAIRSTRANDSCORE_API FHairStrandsInterpolationDatas
{
	/** Serialize the interpolated points */
	void Serialize(FArchive& Ar);

	/** Set the number of interpolated points */
	void SetNum(const uint32 NumPoints);

	/** Reset the interpolated points to 0 */
	void Reset();

	/** Get the number of interpolated points */
	uint32 Num() const { return PointsSimCurvesVertexIndex.Num(); }

	/** Simulation curve indices, ordered by closest influence */
	TArray<FIntVector> PointsSimCurvesIndex;

	/** Closest vertex indices on simulation curve, ordered by closest influence */
	TArray<FIntVector> PointsSimCurvesVertexIndex;

	/** Lerp value between the closest vertex indices and the next one, ordered by closest influence */
	TArray<FVector> PointsSimCurvesVertexLerp;

	/** Weight of vertex indices on simulation curve, ordered by closest influence */
	TArray<FVector>	PointsSimCurvesVertexWeights;

	struct FRenderData
	{
		TArray<FHairStrandsInterpolation0Format::Type> Interpolation0;
		TArray<FHairStrandsInterpolation1Format::Type> Interpolation1;

		void Serialize(FArchive& Ar);
	} RenderData;
};

/** Hair strands points attribute */
struct HAIRSTRANDSCORE_API FHairStrandsPoints
{
	/** Serialize the points */
	void Serialize(FArchive& Ar);

	/** Set the number of points */
	void SetNum(const uint32 NumPoints);

	/** Reset the points to 0 */
	void Reset();

	/** Get the number of points */
	uint32 Num() const { return PointsPosition.Num();  }

	/** Points position in local space */
	TArray<FVector> PointsPosition;

	/** Normalized radius relative to the max one */
	TArray<float> PointsRadius; // [0..1]

	/** Normalized length */
	TArray<float> PointsCoordU; // [0..1]

	/** Material base color */
	TArray<FLinearColor> PointsBaseColor; // [0..1]

	/** Material roughness */
	TArray<float> PointsRoughness; // [0..1]
};

/** Hair strands Curves attribute */
struct HAIRSTRANDSCORE_API FHairStrandsCurves
{
	/** Serialize the curves */
	void Serialize(FArchive& Ar);

	/** Set the number of Curves */
	void SetNum(const uint32 NumPoints);

	/** Reset the curves to 0 */
	void Reset();

	/** Get the number of Curves */
	uint32 Num() const { return CurvesCount.Num(); }

	/** Number of points per rod */
	TArray<uint16> CurvesCount;

	/** An offset represent the rod start in the point list */
	TArray<uint32> CurvesOffset;

	/** Normalized length relative to the max one */
	TArray<float> CurvesLength; // [0..1]

	/** Roots UV. Support UDIM coordinate up to 256x256 */
	TArray<FVector2D> CurvesRootUV; // [0..256]

	/** Max strands Curves length */
	float MaxLength = 0;

	/** Max strands Curves radius */
	float MaxRadius = 0;
};

/** Hair strands datas that are stored on CPU */
struct HAIRSTRANDSCORE_API FHairStrandsDatas
{
	/** Serialize all the hair strands datas */
	void Serialize(FArchive& Ar);

	/* Get the total number of points */
	uint32 GetNumPoints() const { return StrandsPoints.Num(); }

	/* Get the total number of Curves */
	uint32 GetNumCurves() const { return StrandsCurves.Num(); }

	void Reset();

	/** List of all the strands points */
	FHairStrandsPoints StrandsPoints;

	/** List of all the strands curves */
	FHairStrandsCurves StrandsCurves;

	/** The Standard Hair Density */
	float HairDensity = 1;

	/* Strands bounding box */
	FBox BoundingBox;

	struct FRenderData
	{
		TArray<FHairStrandsPositionFormat::Type> RenderingPositions;
		TArray<FHairStrandsAttributeFormat::Type> RenderingAttributes;
		TArray<FHairStrandsMaterialFormat::Type> RenderingMaterials;

		void Serialize(FArchive& Ar);
	} RenderData;
};
