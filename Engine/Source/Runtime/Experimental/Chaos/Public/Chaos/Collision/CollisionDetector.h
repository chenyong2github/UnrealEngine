// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/StatsData.h"

namespace Chaos
{
	class FPBDCollisionConstraints;
	class FNarrowPhase;

	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::Detect"), STAT_Collisions_Detect, STATGROUP_ChaosCollision, CHAOS_API);

	class CHAOS_API FCollisionDetector
	{
	public:
		FCollisionDetector(FNarrowPhase& InNarrowPhase, FPBDCollisionConstraints& InCollisionContainer)
			: NarrowPhase(InNarrowPhase)
			, CollisionContainer(InCollisionContainer)
		{
		}

		virtual ~FCollisionDetector() {}

		FPBDCollisionConstraints& GetCollisionContainer() { return CollisionContainer; }
		FNarrowPhase& GetNarrowPhase() { return NarrowPhase; }

		virtual void DetectCollisionsWithStats(const FReal Dt, CollisionStats::FStatData& StatData) = 0;

		void DetectCollisions(const FReal Dt)
		{
			CollisionStats::FStatData StatData(false);
			DetectCollisionsWithStats(Dt, StatData);
		}

	protected:
		FNarrowPhase& NarrowPhase;
		FPBDCollisionConstraints& CollisionContainer;
		FCollisionContext Context;
	};

}
