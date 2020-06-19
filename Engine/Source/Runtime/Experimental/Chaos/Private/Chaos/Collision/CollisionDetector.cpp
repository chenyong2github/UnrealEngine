// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

namespace Chaos
{
	DEFINE_STAT(STAT_Collisions_Detect);
	DEFINE_STAT(STAT_Collisions_BroadPhase);
	DEFINE_STAT(STAT_Collisions_SpatialBroadPhase);
	DEFINE_STAT(STAT_Collisions_Filtering);
	DEFINE_STAT(STAT_Collisions_ComputeBoundsThickness);
	DEFINE_STAT(STAT_Collisions_GenerateCollisions);
	DEFINE_STAT(STAT_Collisions_ReceiveCollisions);
#if CHAOS_ENABLE_STAT_NARROWPHASE
	DEFINE_STAT(STAT_Collisions_NarrowPhase);
#endif
}