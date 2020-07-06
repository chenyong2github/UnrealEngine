// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenCubeMapTree.h
=============================================================================*/

#pragma once

#include "TextureLayout3d.h"
#include "RenderResource.h"
#include "MeshCardRepresentation.h"
#include "LumenSparseSpanArray.h"

class FPrimitiveSceneInfo;
class FCardSourceData;

struct FLumenCubeMapTreeGPUData
{
	// Must match LUMEN_CUBE_MAP_TREE_DATA_STRIDE in usf
	enum { DataStrideInFloat4s = 8 };
	enum { DataStrideInBytes = DataStrideInFloat4s * 16 };

	static void FillData(const class FLumenCubeMapTree& RESTRICT CubeMapTree, FVector4* RESTRICT OutData);
};

struct FLumenCubeMapGPUData
{
	// Must match usf
	enum { DataStrideInFloat4s = 2 };
	enum { DataStrideInBytes = DataStrideInFloat4s * 16 };

	static void FillData(const class FLumenCubeMap& RESTRICT CubeMap, FVector4* RESTRICT OutData);
};

class FLumenCubeMapTreeLUTAtlas
{
public:
	FLumenCubeMapTreeLUTAtlas();

	void Allocate(TSparseSpanArray<FLumenCubeMapTree>& CubeMapTrees, const TArray<int32>& CubeMapTreeIndicesToAllocate);
	void RemoveAllocation(FLumenCubeMapTree& CubeMapTree);
	FTexture3DRHIRef GetTexture() const { return VolumeTextureRHI; }

private:

	class FAllocation
	{
	public:
		int32 NumRefs;
		FIntVector MinInAtlas;
		FIntVector SizeInAtlas;
	};

	const EPixelFormat VolumeFormat;
	FTextureLayout3d BlockAllocator;
	FTexture3DRHIRef VolumeTextureRHI;
	TMap<FCardRepresentationDataId, FAllocation> AllocationMap;
};

extern FLumenCubeMapTreeLUTAtlas GLumenCubeMapTreeLUTAtlas;

class FLumenCubeMapTree
{
public:
	FLumenCubeMapTree()
		: PrimitiveSceneInfo(nullptr)
		, InstanceIndexOrMergedFlag(0)
		, LocalToWorld(EForceInit::ForceInitToZero)
		, FirstCardIndex(0)
		, NumCards(0)
		, FirstCubeMapIndex(0)
		, NumCubeMaps(0)
		, LUTVolumeBounds(EForceInit::ForceInitToZero)
		, MinInLUTAtlas(0, 0, 0)
		, SizeInLUTAtlas(0, 0, 0)
	{
	}

	void Initialize(FPrimitiveSceneInfo* InPrimitiveSceneInfo, int32 InInstanceIndexOrMergedFlag, const FMatrix& InLocalToWorld, int32 InFirstCardIndex, int32 InNumCards, int32 InFirstCubeMapIndex, int32 InNumCubeMaps, const FBox& InLUTVolumeBounds)
	{
		PrimitiveSceneInfo = InPrimitiveSceneInfo;
		InstanceIndexOrMergedFlag = InInstanceIndexOrMergedFlag;
		FirstCardIndex = InFirstCardIndex;
		NumCards = InNumCards;
		FirstCubeMapIndex = InFirstCubeMapIndex;
		NumCubeMaps = InNumCubeMaps;
		LUTVolumeBounds = InLUTVolumeBounds;
		MinInLUTAtlas = FIntVector(0, 0, 0);
		SizeInLUTAtlas = FIntVector(0, 0, 0);
		SetTransform(InLocalToWorld);
	}

	void SetTransform(const FMatrix& InLocalToWorld)
	{
		LocalToWorld = InLocalToWorld;
	}

	FPrimitiveSceneInfo* PrimitiveSceneInfo;
	// -1 if representing all instances belonging to the primitive (merged instances), otherwise instance index
	int32 InstanceIndexOrMergedFlag;
	FMatrix LocalToWorld;

	int32 FirstCardIndex;
	int32 NumCards;

	int32 FirstCubeMapIndex;
	int32 NumCubeMaps;

	FBox LUTVolumeBounds;
	FCardRepresentationDataId LUTAtlasAllocationId;
	FIntVector MinInLUTAtlas;
	FIntVector SizeInLUTAtlas;
};

class FLumenCubeMap
{
public:
	FLumenCubeMap()
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(FaceCardIndices); ++Index)
		{
			FaceCardIndices[Index] = UINT32_MAX;
		}
	}

	void Initialize(const FLumenCubeMapBuildData& CubeMapBuildData, const TArray<int32, TInlineAllocator<6>>& BuildFaceToCulledFaceIndexBuffer, int32 FirstCardIndex)
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(FaceCardIndices); ++Index)
		{
			const int32 CubeMapFaceIndex = CubeMapBuildData.FaceIndices[Index];
			const int32 CardIndex = CubeMapFaceIndex == -1 ? -1 : BuildFaceToCulledFaceIndexBuffer[CubeMapFaceIndex];
			FaceCardIndices[Index] = CubeMapFaceIndex == -1 ? UINT32_MAX : FirstCardIndex + CardIndex;
		}
	}

	// -X, +X, -Y, +Y, -Z, +Z
	uint32 FaceCardIndices[6];
};

class FLumenCubeMapTreeBounds
{
public:

	static constexpr uint32 MaxCards = 6;

	FLumenCubeMapTreeBounds() = default;

	void InitFromCubeMapTree(const FLumenCubeMapTree& CubeMapTree, const TSparseSpanArray<FCardSourceData>& Cards);
	void UpdateBounds(const FLumenCubeMapTree& CubeMapTree, const TSparseSpanArray<FCardSourceData>& Cards);

	float ComputeSquaredDistanceFromBoxToPoint(const FVector& Point) const
	{
		return ::ComputeSquaredDistanceFromBoxToPoint(WorldBoundsMin, WorldBoundsMax, Point);
	}

	bool HasVisibleCards() const
	{
		return NumVisibleCards != 0;
	}

	int32 GetFirstCardIndex() const
	{
		return FirstCardIndex;
	}

	int32 GetLastCardIndex() const
	{
		return FirstCardIndex + NumCards;
	}

	void IncrementVisible()
	{
		check(NumVisibleCards < NumCards);
		++NumVisibleCards;
	}

	void DecrementVisible()
	{
		check(NumVisibleCards != 0);
		--NumVisibleCards;
	}

	FVector GetWorldBoundsExtent() const
	{
		return WorldBoundsMax - WorldBoundsMin;
	}

	float GetResolutionScale() const
	{
		return ResolutionScale;
	}

private:
	FVector WorldBoundsMin = FVector::ZeroVector;
	FVector WorldBoundsMax = FVector::ZeroVector;
	uint32 FirstCardIndex = 0;
	float ResolutionScale = 1.0f;
	uint8 NumCards = 0;
	uint8 NumVisibleCards = 0;
};

