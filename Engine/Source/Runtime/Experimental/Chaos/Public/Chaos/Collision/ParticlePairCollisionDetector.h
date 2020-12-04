// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"

namespace Chaos
{
	class CHAOS_API FParticlePairCollisionDetector : public FCollisionDetector
	{
	public:
		FParticlePairCollisionDetector(FParticlePairBroadPhase& InBroadPhase, FNarrowPhase& InNarrowPhase, FPBDCollisionConstraints& InCollisionContainer)
			: FCollisionDetector(InNarrowPhase, InCollisionContainer)
			, BroadPhase(InBroadPhase)
		{
		}

		FParticlePairBroadPhase& GetBroadPhase() { return BroadPhase; }

		virtual void DetectCollisionsWithStats(const FReal Dt, CollisionStats::FStatData& StatData, FEvolutionResimCache*) override
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_Detect);
			CHAOS_SCOPED_TIMER(DetectCollisions);

			if (!GetCollisionContainer().GetCollisionsEnabled())
			{
				return;
			}

			CollisionContainer.UpdateConstraints(Dt);

			// Collision detection pipeline: BroadPhase -> NarrowPhase -> Container
			BroadPhase.ProduceOverlaps(Dt, CollisionContainer.GetConstraintsArray(), NarrowPhase, StatData);
		}

	private:
		FParticlePairBroadPhase& BroadPhase;
	};

}