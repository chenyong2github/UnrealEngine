// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

namespace Chaos
{
	DEFINE_STAT(STAT_Collisions_Detect);
	DEFINE_STAT(STAT_Collisions_BroadPhase);
	DEFINE_STAT(STAT_Collisions_NarrowPhase);

	template class TCollisionDetector<FParticlePairBroadPhase, FNarrowPhase, FSyncCollisionReceiver, TPBDCollisionConstraints<FReal, 3>>;
	template class TCollisionDetector<FSpatialAccelerationBroadPhase, FNarrowPhase, FAsyncCollisionReceiver, TPBDCollisionConstraints<FReal, 3>>;
}