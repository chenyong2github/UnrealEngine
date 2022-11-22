// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/EvolutionResimCache.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "ProfilingDebugging/CsvProfiler.h"

namespace Chaos
{
	class CHAOS_API FSpatialAccelerationCollisionDetector : public FCollisionDetector
	{
	public:
		FSpatialAccelerationCollisionDetector(FSpatialAccelerationBroadPhase& InBroadPhase, FPBDCollisionConstraints& InCollisionContainer)
			: FCollisionDetector(InCollisionContainer)
			, BroadPhase(InBroadPhase)
		{
		}

		FSpatialAccelerationBroadPhase& GetBroadPhase()
		{ 
			return BroadPhase;
		}

		virtual void DetectCollisions(const FReal Dt, FEvolutionResimCache* ResimCache) override
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_Detect);
			CHAOS_SCOPED_TIMER(DetectCollisions);
			CSV_SCOPED_TIMING_STAT(Chaos, DetectCollisions);

			if (!GetCollisionContainer().GetCollisionsEnabled())
			{
				return;
			}

			GetCollisionContainer().BeginDetectCollisions();

			// Collision detection pipeline: BroadPhase -[parallel]-> NarrowPhase -[parallel]-> CollisionAllocator -[serial]-> Container
			BroadPhase.ProduceOverlaps(Dt, &GetCollisionContainer().GetConstraintAllocator(), GetCollisionContainer().GetDetectorSettings(), ResimCache);

			GetCollisionContainer().EndDetectCollisions();

			// If we have a resim cache restore and save contacts
			if(ResimCache)
			{
				// Ensure we have at least one allocator
				GetCollisionContainer().GetConstraintAllocator().SetMaxContexts(1);

				FCollisionContext Context;
				Context.SetSettings(GetCollisionContainer().GetDetectorSettings());
				Context.SetAllocator(GetCollisionContainer().GetConstraintAllocator().GetContextAllocator(0));

				GetCollisionContainer().GetConstraintAllocator().AddResimConstraints(ResimCache->GetAndSanitizeConstraints(), Context);

				ResimCache->SaveConstraints(GetCollisionContainer().GetConstraints());
			}
		}

	private:
		FSpatialAccelerationBroadPhase& BroadPhase;
	};
}
