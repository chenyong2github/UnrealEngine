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

namespace Lumen
{
	void UpdateCardSceneBuffer(FRHICommandListImmediate& RHICmdList, const FSceneViewFamily& ViewFamily, FScene* Scene);
};

class FLumenMeshCards
{
public:
	void Initialize(
		const FMatrix& InLocalToWorld, 
		const FBox& InBounds,
		int32 InPrimitiveGroupIndex,
		uint32 InFirstCardIndex,
		uint32 InNumCards,
		uint32 InNumCardsPerOrientation[6],
		uint32 InCardOffsetPerOrientation[6])
	{
		PrimitiveGroupIndex = InPrimitiveGroupIndex;

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

	int32 PrimitiveGroupIndex = -1;

	uint32 FirstCardIndex = 0;
	uint32 NumCards = 0;
	uint32 NumCardsPerOrientation[6];
	uint32 CardOffsetPerOrientation[6];
};