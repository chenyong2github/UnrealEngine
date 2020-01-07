// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/CollisionDetector.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

namespace Chaos
{
	DEFINE_STAT(STAT_DetectCollisions);

	template class TCollisionDetector<FParticlePairBroadPhase, FNarrowPhase, FSyncCollisionReceiver, TPBDCollisionConstraints<FReal, 3>>;
	template class TCollisionDetector<FSpatialAccelerationBroadPhase, FNarrowPhase, FAsyncCollisionReceiver, TPBDCollisionConstraints<FReal, 3>>;
}