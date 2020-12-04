// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/EvolutionResimCache.h"

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

		virtual void DetectCollisionsWithStats(const FReal Dt, CollisionStats::FStatData& StatData, FEvolutionResimCache* ResimCache) override
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_Detect);
			CHAOS_SCOPED_TIMER(DetectCollisions);

			if (!GetCollisionContainer().GetCollisionsEnabled())
			{
				return;
			}

			CollisionContainer.UpdateConstraints(Dt);

			// Collision detection pipeline: BroadPhase -[parallel]-> NarrowPhase -[parallel]-> Receiver -[serial]-> Container
			FAsyncCollisionReceiver Receiver(CollisionContainer, ResimCache);
			BroadPhase.ProduceOverlaps(Dt, NarrowPhase, Receiver, StatData, ResimCache);
			if(ResimCache)
			{
				// Push the resim constraints into slot zero with the first particle. Doesn't really matter where they go at this
				// point as long as it is consistent so we don't need to sort the constraints in ProcessCollisions
				if(Receiver.CacheNum() == 0)
				{
					// In case we have zero particles but somehow have constraints
					Receiver.Prepare(1);
				}

				Receiver.AppendCollisions(ResimCache->GetAndSanitizeConstraints(), 0);
			}
			Receiver.ProcessCollisions();
		}

	private:
		FSpatialAccelerationBroadPhase& BroadPhase;
	};
}
