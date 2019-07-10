// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDCollisionTypes.h"

namespace Chaos
{
	void CHAOS_API ComputeHashTable(const TArray<Chaos::TPBDCollisionConstraint<float, 3>::FRigidBodyContactConstraint>& ConstraintsArray,
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
