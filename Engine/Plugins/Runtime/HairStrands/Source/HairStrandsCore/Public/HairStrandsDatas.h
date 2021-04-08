// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "RHIDefinitions.h"
#include "PackedNormal.h"
#include "RenderGraphResources.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHairStrands, Log, All);

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
FArchive& operator<<(FArchive& Ar, FVector4_16& Vertex);

struct FHairStrandsPositionFormat
{
	typedef FPackedHairVertex Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_UShort4;
	static const EPixelFormat Format = PF_R16G16B16A16_UINT;
};

struct FHairStrandsPositionOffsetFormat
{
	typedef FVector4 Type;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
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
 */
struct FHairStrandsMeshTrianglePositionFormat
{
	using Type = FVector4;
	static const uint32 ComponentCount = 1;
	static const uint32 SizeInByte = sizeof(Type);
	static const EVertexElementType VertexElementType = VET_Float4;
	static const EPixelFormat Format = PF_A32B32G32R32F;
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

struct FHairStrandsRootUtils
{
	static uint32	 EncodeTriangleIndex(uint32 TriangleIndex, uint32 SectionIndex);
	static void		 DecodeTriangleIndex(uint32 Encoded, uint32& OutTriangleIndex, uint32& OutSectionIndex);
	static uint32	 EncodeBarycentrics(const FVector2D& B);
	static FVector2D DecodeBarycentrics(uint32 B);
	static uint32	 PackUVs(const FVector2D& UV);
	static float	 PackUVsToFloat(const FVector2D& UV);
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

	/** Strand ID associated with each curve */
	TArray<int> StrandIDs;

	/** Mapping of imported Groom ID to index */
	TMap<int, int> GroomIDToIndex;

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
		TArray<FHairStrandsPositionFormat::Type>	Positions;
		TArray<FHairStrandsAttributeFormat::Type>	Attributes;
		TArray<FHairStrandsMaterialFormat::Type>	Materials;
		TArray<	FHairStrandsRootIndexFormat::Type>	RootIndices; // Vertex to strands Index

		void Serialize(FArchive& Ar);
	} RenderData;
};

/** Hair strands debug data */
struct HAIRSTRANDSCORE_API FHairStrandsDebugDatas
{
	bool IsValid() const { return VoxelData.Num() > 0;  }

	static const uint32 InvalidIndex = ~0u;
	struct FOffsetAndCount
	{
		uint32 Offset = 0u;
		uint32 Count = 0u;
	};

	struct FVoxel
	{
		uint32 Index0 = InvalidIndex;
		uint32 Index1 = InvalidIndex;
	};

	struct FDesc
	{
		FVector VoxelMinBound;
		FVector VoxelMaxBound;
		FIntVector VoxelResolution;
		float VoxelSize;
	};

	FDesc VoxelDescription;
	TArray<FOffsetAndCount> VoxelOffsetAndCount;
	TArray<FVoxel> VoxelData;

	struct FResources
	{
		FDesc VoxelDescription;

		TRefCountPtr<FRDGPooledBuffer> VoxelOffsetAndCount;
		TRefCountPtr<FRDGPooledBuffer> VoxelData;
	};
};

/* Source/CPU data for root resources (GPU resources are stored into FHairStrandsRootResources) */
struct FHairStrandsRootData
{
	/** Build the hair strands resource */
	FHairStrandsRootData();
	FHairStrandsRootData(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount, const TArray<uint32>& NumSamples);
	void Serialize(FArchive& Ar);
	void Reset();
	bool HasProjectionData() const;
	bool IsValid() const { return RootCount > 0; }

	struct FMeshProjectionLOD
	{
		int32 LODIndex = -1;

		/* Triangle on which a root is attached */
		/* When the projection is done with source to target mesh transfer, the projection indices does not match.
		   In this case we need to separate index computation. The barycentric coords remain the same however. */
		TArray<FHairStrandsCurveTriangleIndexFormat::Type> RootTriangleIndexBuffer;
		TArray<FHairStrandsCurveTriangleBarycentricFormat::Type> RootTriangleBarycentricBuffer;

		/* Strand hair roots translation and rotation in rest position relative to the bound triangle. Positions are relative to the rest root center */
		TArray<FHairStrandsMeshTrianglePositionFormat::Type> RestRootTrianglePosition0Buffer;
		TArray<FHairStrandsMeshTrianglePositionFormat::Type> RestRootTrianglePosition1Buffer;
		TArray<FHairStrandsMeshTrianglePositionFormat::Type> RestRootTrianglePosition2Buffer;

		/* Number of samples used for the mesh interpolation */
		uint32 SampleCount = 0;

		/* Store the hair interpolation weights | Size = SamplesCount * SamplesCount */
		TArray<FHairStrandsWeightFormat::Type> MeshInterpolationWeightsBuffer;

		/* Store the samples vertex indices */
		TArray<FHairStrandsIndexFormat::Type> MeshSampleIndicesBuffer;

		/* Store the samples rest positions */
		TArray<FHairStrandsMeshTrianglePositionFormat::Type> RestSamplePositionsBuffer;

		/* Store the mesh section indices which are relevant for this root LOD data */
		TArray<uint32> ValidSectionIndices;
	};

	/* Number of roots */
	uint32 RootCount = 0;

	/* Curve index for every vertices */
	TArray<FHairStrandsIndexFormat::Type> VertexToCurveIndexBuffer;

	/* Curve root's positions */
	TArray<FHairStrandsRootPositionFormat::Type> RootPositionBuffer;

	/* Curve root's normal orientation */
	TArray<FHairStrandsRootNormalFormat::Type> RootNormalBuffer;

	/* Store the hair projection information for each mesh LOD */
	TArray<FMeshProjectionLOD> MeshProjectionLODs;
};

struct HAIRSTRANDSCORE_API FHairStrandsClusterCullingData
{
	FHairStrandsClusterCullingData();
	void Reset();
	void Serialize(FArchive& Ar);
	bool IsValid() const { return ClusterCount > 0 && VertexCount > 0; }

	static const uint32 MaxLOD = 8;

	/* Structure describing the LOD settings (Screen size, vertex info, ...) for each clusters.
		The packed version of this structure corresponds to the GPU data layout (HairStrandsClusterCommon.ush)
		This uses by the GPU LOD selection. */
	struct FHairClusterInfo
	{
		struct Packed
		{
			uint32 LODInfoOffset : 24;
			uint32 LODCount : 8;

			uint32 LOD_ScreenSize_0 : 10;
			uint32 LOD_ScreenSize_1 : 10;
			uint32 LOD_ScreenSize_2 : 10;
			uint32 Pad0 : 2;

			uint32 LOD_ScreenSize_3 : 10;
			uint32 LOD_ScreenSize_4 : 10;
			uint32 LOD_ScreenSize_5 : 10;
			uint32 Pad1 : 2;

			uint32 LOD_ScreenSize_6 : 10;
			uint32 LOD_ScreenSize_7 : 10;
			uint32 LOD_bIsVisible : 8;
			uint32 Pad2 : 4;
		};

		FHairClusterInfo()
		{
			for (uint32 LODIt = 0; LODIt < MaxLOD; ++LODIt)
			{
				ScreenSize[LODIt] = 0;
				bIsVisible[LODIt] = true;
			}
		}

		uint32 LODCount = 0;
		uint32 LODInfoOffset = 0;
		TStaticArray<float, MaxLOD>  ScreenSize;
		TStaticArray<bool, MaxLOD>   bIsVisible;
	};

	/* Structure describing the LOD settings common to all clusters. The layout of this structure is
		identical the GPU data layout (HairStrandsClusterCommon.ush). This uses by the GPU LOD selection. */
	struct FHairClusterLODInfo
	{
		uint32 VertexOffset = 0;
		uint32 VertexCount0 = 0;
		uint32 VertexCount1 = 0;
		FFloat16 RadiusScale0 = 0;
		FFloat16 RadiusScale1 = 0;
	};

	/* Set LOD visibility, allowing to remove the simulation/rendering of certain LOD */
	TArray<bool>				LODVisibility;

	/* Screen size at which LOD should switches on CPU */
	TArray<float>				CPULODScreenSize;

	/* LOD info for the various clusters for LOD management on GPU */
	TArray<FHairClusterInfo>	ClusterInfos;
	TArray<FHairClusterLODInfo> ClusterLODInfos;
	TArray<uint32>				VertexToClusterIds;
	TArray<uint32>				ClusterVertexIds;

	/* Packed LOD info packed into GPU format */
	TArray<FHairClusterInfo::Packed> PackedClusterInfos;

	/* Number of cluster  */
	uint32 ClusterCount = 0;

	/* Number of vertex  */
	uint32 VertexCount = 0;
};
