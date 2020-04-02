// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"

namespace Chaos
{
	class CHAOS_API FSpatialAccelerationCollisionDetector : public FCollisionDetector
	{
	public:
		FSpatialAccelerationCollisionDetector(FSpatialAccelerationBroadPhase& InBroadPhase, FNarrowPhase& InNarrowPhase, FPBDCollisionConstraints& InCollisionContainer)
			: FCollisionDetector(InNarrowPhase, InCollisionContainer)
			, BroadPhase(InBroadPhase)
		{
		}

		FSpatialAccelerationBroadPhase& GetBroadPhase() { return BroadPhase; }

		virtual void DetectCollisionsWithStats(const FReal Dt, CollisionStats::FStatData& StatData) override
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_Detect);
			CHAOS_SCOPED_TIMER(DetectCollisions);

			if (!GetCollisionContainer().GetCollisionsEnabled())
			{
				return;
			}

			CollisionContainer.UpdateManifolds(Dt);
			CollisionContainer.UpdateConstraints(Dt);

			// Collision detection pipeline: BroadPhase -[parallel]-> NarrowPhase -[parallel]-> Receiver -[serial]-> Container
			FAsyncCollisionReceiver Receiver(CollisionContainer);
			BroadPhase.ProduceOverlaps(Dt, NarrowPhase, Receiver, StatData);
			Receiver.ProcessCollisions();
		}

	private:
		FSpatialAccelerationBroadPhase& BroadPhase;
	};
}
