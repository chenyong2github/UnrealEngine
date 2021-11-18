// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionContext.h"

#include "ChaosStats.h"

namespace Chaos
{
	class FPBDCollisionConstraints;
	class FNarrowPhase;
	class FEvolutionResimCache;

	class CHAOS_API FCollisionDetector
	{
	public:
		FCollisionDetector(FPBDCollisionConstraints& InCollisionContainer)
			: CollisionContainer(InCollisionContainer)
		{
		}

		virtual ~FCollisionDetector() {}

		FPBDCollisionConstraints& GetCollisionContainer() { return CollisionContainer; }

		virtual void DetectCollisions(const FReal Dt, FEvolutionResimCache* ResimCache) = 0;

	protected:
		FPBDCollisionConstraints& CollisionContainer;
	};

}
