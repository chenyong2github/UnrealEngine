// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "RHIDefinitions.h"
#include "PackedNormal.h"

DEFINE_LOG_CATEGORY_STATIC(LogHairStrands, Log, All);

/**  A packed position for the hair strands datas .*/
struct HAIRSTRANDSCORE_API FHairStrandsPackedPosition
{
	// #todo_hair: repack the position into relative 16 bits position. These position should be relative to the AABB center. 
	// If Float point is not enough we could use fixed point value
#if 0 
	//FFloat16 X, Y, Z; 
	//uint16 ControlPointType : 2; 
	//uint16 NormalizedRadius : 6;
	//uint16 UCoord : 8;
#else
	float X, Y, Z;
	uint32 ControlPointType : 2;
	uint32 NormalizedRadius : 6;
	uint32 UCoord : 8;
	uint32 NormalizedLength : 8;
	uint32 Seed : 8;
#endif
};

/** Hair strands position format */
struct HAIRSTRANDSCORE_API FHairStrandsPositionFormat
{
	using Type = FHairStrandsPackedPosition;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;	// VET_UShort4;
	static const EPixelFormat Format = PF_A32B32G32R32F;			// PF_R16G16B16A16_UINT;
};

/** Hair Strands tangent format */
struct HAIRSTRANDSCORE_API FHairStrandsTangentFormat
{
	// TangentX & tangentZ are packed into 2 * PF_R8G8B8A8_SNORM
	using Type = FPackedNormal;
	static const uint32 ComponentCount = 2;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_R8G8B8A8_SNORM;
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

/** Hair strands quaternion format */
struct HAIRSTRANDSCORE_API FHairStrandsQuaternionFormat
{
	using Type = FQuat;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;	
	static const EPixelFormat Format = PF_A32B32G32R32F;			
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

	/** Normalized length relative to the strand one */
	TArray<float> PointsCoordU; // [0..1]

	/** Max strands points radius */
	float MaxRadius = 0;
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

	/** Max strands Curves length */
	float MaxLength = 0;
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

	/** List of all the strands points */
	FHairStrandsPoints StrandsPoints;

	/** List of all the strands points */
	FHairStrandsCurves StrandsCurves;

	/** The Standard Hair Density */
	float HairDensity = 1;

	/* Strands bounding box */
	FBox BoundingBox;

	/** Build the packed datas for gpu rendering/simulation */
	void BuildRenderingDatas(
		TArray<FHairStrandsPositionFormat::Type>& OutPointsPositions,
		TArray<FHairStrandsTangentFormat::Type>& OutPointsTangents) const;

	/** Resample the internal points and curves simulation datas */
	void BuildSimulationDatas(const uint32 StrandSize,
		TArray<FHairStrandsPositionFormat::Type>& OutNodesPositions,
		TArray<FHairStrandsWeightFormat::Type>& OutPointsWeights,
		TArray<FHairStrandsIndexFormat::Type>& OutPointsNodes) const;

	/** Build the internal points and curves datas */
	void BuildInternalDatas();

	/** Attach the roots to a static mesh */
	void AttachStrandsRoots(UStaticMesh* StaticMesh, const FMatrix& TransformMartrix);
};

void FHairStrandsCurves::SetNum(const uint32 NumCurves)
{
	CurvesOffset.SetNum(NumCurves + 1);
	CurvesCount.SetNum(NumCurves);
	CurvesLength.SetNum(NumCurves);
	CurvesRootUV.SetNum(NumCurves);
}

void FHairStrandsPoints::SetNum(const uint32 NumPoints)
{
	PointsPosition.SetNum(NumPoints);
	PointsRadius.SetNum(NumPoints);
	PointsCoordU.SetNum(NumPoints);
}

void FHairStrandsCurves::Reset()
{
	CurvesOffset.Reset();
	CurvesCount.Reset();
	CurvesLength.Reset();
	CurvesRootUV.Reset();
}

void FHairStrandsPoints::Reset()
{
	PointsPosition.Reset();
	PointsRadius.Reset();
	PointsCoordU.Reset();
}

void FHairStrandsPoints::Serialize(FArchive& Ar)
{
	Ar << PointsPosition;
	Ar << PointsRadius;
	Ar << PointsCoordU;
	Ar << MaxRadius;
}

void FHairStrandsCurves::Serialize(FArchive& Ar)
{
	Ar << CurvesCount;
	Ar << CurvesOffset;
	Ar << CurvesLength;
	Ar << CurvesRootUV;
	Ar << MaxLength;
}

void FHairStrandsDatas::Serialize(FArchive& Ar)
{
	StrandsPoints.Serialize(Ar);
	StrandsCurves.Serialize(Ar);
	Ar << HairDensity;
	Ar << BoundingBox;
}


