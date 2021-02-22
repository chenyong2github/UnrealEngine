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
class FLumenCard;

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
	void Initialize(
		const FMatrix& InLocalToWorld, 
		const FBox& InBounds,
		uint32 InFirstCardIndex,
		uint32 InNumCards,
		uint32 InNumCardsPerOrientation[6],
		uint32 InCardOffsetPerOrientation[6])
	{
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

	FMatrix LocalToWorld;
	FBox Bounds;

	uint32 FirstCardIndex = 0;
	uint32 NumCards = 0;
	uint32 NumCardsPerOrientation[6];
	uint32 CardOffsetPerOrientation[6];
};