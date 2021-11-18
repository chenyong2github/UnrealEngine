// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/EvolutionResimCache.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "ProfilingDebugging/CsvProfiler.h"

namespace Chaos
{
	class CHAOS_API FSpatialAccelerationCollisionDetector : public FCollisionDetector
	{
	public:
		FSpatialAccelerationCollisionDetector(FSpatialAccelerationBroadPhase& InBroadPhase, FNarrowPhase& InNarrowPhase, FPBDCollisionConstraints& InCollisionContainer)
			: FCollisionDetector(InCollisionContainer)
			, BroadPhase(InBroadPhase)
			, NarrowPhase(InNarrowPhase)
		{
		}

		FSpatialAccelerationBroadPhase& GetBroadPhase() { return BroadPhase; }
		FNarrowPhase& GetNarrowPhase() { return NarrowPhase; }

		virtual void DetectCollisions(const FReal Dt, FEvolutionResimCache* ResimCache) override
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_Detect);
			CHAOS_SCOPED_TIMER(DetectCollisions);
			CSV_SCOPED_TIMING_STAT(Chaos, DetectCollisions);

			if (!GetCollisionContainer().GetCollisionsEnabled())
			{
				return;
			}

			CollisionContainer.BeginDetectCollisions();

			// Collision detection pipeline: BroadPhase -[parallel]-> NarrowPhase -[parallel]-> CollisionAllocator -[serial]-> Container
			BroadPhase.ProduceOverlaps(Dt, NarrowPhase, ResimCache);

			CollisionContainer.EndDetectCollisions();

			// If we have a resim cache restore and save contacts
			if(ResimCache)
			{
				CollisionContainer.GetConstraintAllocator().AddResimConstraints(ResimCache->GetAndSanitizeConstraints());

				ResimCache->SaveConstraints(CollisionContainer.GetConstraints());
			}
		}

	private:
		FSpatialAccelerationBroadPhase& BroadPhase;
		FNarrowPhase& NarrowPhase;
	};
}
