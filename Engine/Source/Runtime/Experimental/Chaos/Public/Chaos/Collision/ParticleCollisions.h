// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"

#include "Chaos/Collision/CollisionVisitor.h"
#include "Chaos/ParticleHandleFwd.h"


namespace Chaos
{
	class FParticlePairMidPhase;

	/**
	 * @brief Knows about all the collisions detectors associated with a particular particle.
	 * Used when particles are destroyed to remove perisstent collisions from the system, or
	 * when Islands are woken to restore the collisions.
	*/
	class CHAOS_API FParticleCollisions
	{
	public:
		FParticleCollisions();
		~FParticleCollisions();

		TArrayView<FParticlePairMidPhase*> GetParticlePairs()
		{
			return MakeArrayView(ParticlePairs);
		}

		void Reset();

		void AddParticlePair(FParticlePairMidPhase* MidPhase);
		void RemoveParticlePair(FParticlePairMidPhase* MidPhase);

		void VisitCollisions(const FPBDCollisionVisitor& Visitor) const;

	private:
		TArray<FParticlePairMidPhase*> ParticlePairs;
	};

}