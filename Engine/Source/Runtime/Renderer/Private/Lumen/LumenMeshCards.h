// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenMeshCards.h
=============================================================================*/

#pragma once

#include "TextureLayout3d.h"
#include "RenderResource.h"
#include "MeshCardRepresentation.h"
#include "LumenSparseSpanArray.h"

class FLumenCard;
class FPrimitiveSceneInfo;

namespace Lumen
{
	constexpr uint32 NumAxisAlignedDirections = 6;

	void UpdateCardSceneBuffer(FRDGBuilder& GraphBuilder, const FSceneViewFamily& ViewFamily, FScene* Scene);
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
		bool InFarField,
		bool InLandscape)
	{
		PrimitiveGroupIndex = InPrimitiveGroupIndex;

		Bounds = InBounds;
		SetTransform(InLocalToWorld);
		FirstCardIndex = InFirstCardIndex;
		NumCards = InNumCards;
		bFarField = InFarField;
		bLandscape = InLandscape;
	}

	void UpdateLookup(const TSparseSpanArray<FLumenCard>& Cards);

	void SetTransform(const FMatrix& InLocalToWorld)
	{
		LocalToWorld = InLocalToWorld;
	}

	FMatrix LocalToWorld;
	FBox Bounds;

	int32 PrimitiveGroupIndex = -1;
	bool bFarField = false;
	bool bLandscape = false;

	uint32 FirstCardIndex = 0;
	uint32 NumCards = 0;
	uint32 CardLookup[Lumen::NumAxisAlignedDirections];
};