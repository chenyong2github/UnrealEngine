// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/CollisionResolutionTypes.h"

namespace Chaos
{
	void CHAOS_API ComputeHashTable(const TArray<Chaos::TPBDCollisionConstraints<float, 3>::FPointContactConstraint>& ConstraintsArray,
									const FBox& BoundingBox, TMultiMap<int32, int32>& HashTableMap, const float SpatialHashRadius);

	void CHAOS_API ComputeHashTable(const TArray<TCollisionData<float, 3>>& CollisionsArray,
									const FBox& BoundingBox, TMultiMap<int32, int32>& HashTableMap, const float SpatialHashRadius);

	void CHAOS_API ComputeHashTable(const TArray<TCollisionDataExt<float, 3>>& CollisionsArray,
									const FBox& BoundingBox, TMultiMap<int32, int32>& HashTableMap, const float SpatialHashRadius);

	void CHAOS_API ComputeHashTable(const TArray<FVector>& ParticleArray, const FBox& BoundingBox, 
									TMultiMap<int32, int32>& HashTableMap, const float SpatialHashRadius);

	void CHAOS_API ComputeHashTable(const TArray<TBreakingData<float, 3>>& BreakingsArray,
									const FBox& BoundingBox, TMultiMap<int32, int32>& HashTableMap, const float SpatialHashRadius);

	void CHAOS_API ComputeHashTable(const TArray<TBreakingDataExt<float, 3>>& BreakingsArray,
									const FBox& BoundingBox, TMultiMap<int32, int32>& HashTableMap, const float SpatialHashRadius);
}
