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
	uint8 Pad0;
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
	static const EVertexElementType VertexElementType = VET_UByte4;
	static const EPixelFormat Format = PF_R8G8B8A8_UINT;
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
struct HAIRSTRANDSCORE_API FHairStrandsWeightFormat
{
	using Type = float;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float1;
	static const EPixelFormat Format = PF_R32_FLOAT;
};

/** Hair strands points interpolation attributes */
struct HAIRSTRANDSCORE_API FHairStrandsInterpolationDatas
{
	/** Serialize the interpolated points */
	inline void Serialize(FArchive& Ar);

	/** Set the number of interpolated points */
	inline void SetNum(const uint32 NumPoints);

	/** Reset the interpolated points to 0 */
	inline void Reset();

	/** Get the number of interpolated points */
	inline uint32 Num() const { return PointsSimCurvesVertexIndex.Num(); }

	/** Simulation curve indices, ordered by closest influence */
	TArray<FIntVector> PointsSimCurvesIndex;

	/** Closest vertex indices on simulation curve, ordered by closest influence */
	TArray<FIntVector> PointsSimCurvesVertexIndex;

	/** Weight of vertex indices on simulation curve, ordered by closest influence */
	TArray<FVector>	PointsSimCurvesVertexWeights;

	void BuildInterpolationDatas(
		const struct FHairStrandsDatas& SimStrandsData,
		const struct FHairStrandsDatas& RenderStrandsData);

	/** Build data for interpolation between simulation and rendering */
	void BuildRenderingDatas(
		TArray<FHairStrandsInterpolation0Format::Type>& OutPointsInterpolation0,
		TArray<FHairStrandsInterpolation1Format::Type>& OutPointsInterpolation1) const;
};

/** Hair strands points attribute */
struct HAIRSTRANDSCORE_API FHairStrandsPoints
{
	/** Serialize the points */
	inline void Serialize(FArchive& Ar);

	/** Set the number of points */
	inline void SetNum(const uint32 NumPoints);

	/** Reset the points to 0 */
	inline void Reset();

	/** Get the number of points */
	inline uint32 Num() const { return PointsPosition.Num();  }

	/** Points position in local space */
	TArray<FVector> PointsPosition;

	/** Normalized radius relative to the max one */
	TArray<float> PointsRadius; // [0..1]

	/** Normalized length */
	TArray<float> PointsCoordU; // [0..1]
};

/** Hair strands Curves attribute */
struct HAIRSTRANDSCORE_API FHairStrandsCurves
{
	/** Serialize the curves */
	inline void Serialize(FArchive& Ar);

	/** Set the number of Curves */
	inline void SetNum(const uint32 NumPoints);

	/** Reset the curves to 0 */
	inline void Reset();

	/** Get the number of Curves */
	inline uint32 Num() const { return CurvesLength.Num(); }

	/** Number of points per rod */
	TArray<uint16> CurvesCount;

	/** An offset represent the rod start in the point list */
	TArray<uint32> CurvesOffset;

	/** Normalized length relative to the max one */
	TArray<float> CurvesLength; // [0..1]

	/** Roots UV in the bounding sphere */
	TArray<FVector2D> CurvesRootUV; // [0..1]

	TArray<uint32> CurvesGroupID;

	/** Max strands Curves length */
	float MaxLength = 0;

	/** Max strands Curves radius */
	float MaxRadius = 0;
};

/** Hair strands datas that are stored on CPU */
struct HAIRSTRANDSCORE_API FHairStrandsDatas
{
	/** Serialize all the hair strands datas */
	inline void Serialize(FArchive& Ar);

	/* Get the total number of points */
	inline uint32 GetNumPoints() const { return StrandsPoints.Num(); }

	/* Get the total number of Curves */
	inline uint32 GetNumCurves() const { return StrandsCurves.Num(); }

	void Reset();

	/** List of all the strands points */
	FHairStrandsPoints StrandsPoints;

	/** List of all the strands curves */
	FHairStrandsCurves StrandsCurves;

	/** The Standard Hair Density */
	float HairDensity = 1;

	/* Strands bounding box */
	FBox BoundingBox;

	/** Build the packed datas for gpu rendering/simulation */
	void BuildRenderingDatas(
		TArray<FHairStrandsPositionFormat::Type>& OutPointsPositions,
		TArray<FHairStrandsAttributeFormat::Type>& OutPackedAttributes) const;

	/** Resample the internal points and curves simulation datas */
	void BuildSimulationDatas(const uint32 StrandSize,
		TArray<FHairStrandsPositionFormat::Type>& OutNodesPositions,
		TArray<FHairStrandsWeightFormat::Type>& OutPointsWeights,
		TArray<FHairStrandsIndexFormat::Type>& OutPointsNodes) const;

	/** Build the internal points and curves datas */
	void BuildInternalDatas(bool bComputeRootUV = false);

	/** Attach the roots to a static mesh */
	void AttachStrandsRoots(UStaticMesh* StaticMesh, const FMatrix& TransformMartrix);
};

// #ueent_todo: Move the data building and decimation code to a HairBuilder
void HAIRSTRANDSCORE_API DecimateStrandData(const FHairStrandsDatas& InData, float DecimationPercentage, FHairStrandsDatas& OutData);
