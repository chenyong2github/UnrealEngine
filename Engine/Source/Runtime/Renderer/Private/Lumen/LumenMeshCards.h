// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenMeshCards.h
=============================================================================*/

#pragma once

#include "TextureLayout3d.h"
#include "RenderResource.h"
#include "MeshCardRepresentation.h"
#include "LumenSparseSpanArray.h"

class FPrimitiveSceneInfo;
class FCardSourceData;

struct FLumenMeshCardsGPUData
{
	// Must match LUMEN_MESH_CARDS_DATA_STRIDE in usf
	enum { DataStrideInFloat4s = 4 };
	enum { DataStrideInBytes = DataStrideInFloat4s * 16 };

	static void FillData(const class FLumenMeshCards& RESTRICT MeshCards, FVector4* RESTRICT OutData);
};

class FLumenMeshCards
{
public:
	void Initialize(FPrimitiveSceneInfo* InPrimitiveSceneInfo, 
		int32 InInstanceIndexOrMergedFlag, 
		const FMatrix& InLocalToWorld, 
		const FBox& InBounds,
		uint32 InFirstCardIndex,
		uint32 InNumCards,
		uint32 InNumCardsPerOrientation[6],
		uint32 InCardOffsetPerOrientation[6])
	{
		PrimitiveSceneInfo = InPrimitiveSceneInfo;
		InstanceIndexOrMergedFlag = InInstanceIndexOrMergedFlag;
		Bounds = InBounds;
		SetTransform(InLocalToWorld);
		FirstCardIndex = InFirstCardIndex;
		NumCards = InNumCards;

		for (uint32 OrientationIndex = 0; OrientationIndex < 6; ++OrientationIndex)
		{
			NumCardsPerOrientation[OrientationIndex] = InNumCardsPerOrientation[OrientationIndex];
			CardOffsetPerOrientation[OrientationIndex] = InCardOffsetPerOrientation[OrientationIndex];
		}
	}

	void SetTransform(const FMatrix& InLocalToWorld)
	{
		LocalToWorld = InLocalToWorld;
	}

	FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr;
	// -1 if representing all instances belonging to the primitive (merged instances), otherwise instance index
	int32 InstanceIndexOrMergedFlag = 0;
	FMatrix LocalToWorld;
	FBox Bounds;

	uint32 FirstCardIndex = 0;
	uint32 NumCards = 0;
	uint32 NumCardsPerOrientation[6];
	uint32 CardOffsetPerOrientation[6];
};

class FLumenMeshCardsBounds
{
public:

	FLumenMeshCardsBounds() = default;

	void InitFromMeshCards(const FLumenMeshCards& MeshCards, const TSparseSpanArray<FCardSourceData>& Cards);
	void UpdateBounds(const FLumenMeshCards& MeshCards, const TSparseSpanArray<FCardSourceData>& Cards);

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
	uint16 NumCards = 0;
	uint16 NumVisibleCards = 0;
};

