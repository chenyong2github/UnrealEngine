// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/BoundingVolumeUtilities.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosStats.h"

namespace Chaos
{
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::BroadPhase"), STAT_Collisions_BroadPhase, STATGROUP_ChaosCollision, CHAOS_API);

	/**
	 * Run through a list of particle pairs and pass them onto the collision detector if their AABBs overlap.
	 * No spatial acceleration, and the order is assumed to be already optimized for cache efficiency.
	 */
	class CHAOS_API FParticlePairBroadPhase
	{
	public:
		using FParticlePair = TVector<TGeometryParticleHandle<FReal, 3>*, 2>;
		using FAABB = TAABB<FReal, 3>;

		FParticlePairBroadPhase(const TArray<FParticlePair>& InParticlePairs, const FReal InCullDistance)
			: ParticlePairs(InParticlePairs)
			, CullDistance(InCullDistance)
		{
		}

		FReal GetCullDistance() const 
		{
			return CullDistance;
		}

		void SetCullDustance(const FReal InCullDistance)
		{
			CullDistance = InCullDistance;
		}

		/**
		 *
		 */
		void ProduceOverlaps(FReal Dt,
			FCollisionConstraintsArray& ConstraintsArray,
			FNarrowPhase& NarrowPhase,
			CollisionStats::FStatData& StatData)
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_BroadPhase);

			int32 NumPairs = ParticlePairs.Num();
			for (int32 PairIndex = 0; PairIndex < NumPairs; ++PairIndex)
			{
				const FParticlePair& ParticlePair = ParticlePairs[PairIndex];

				const TAABB<FReal, 3>& Box0 = ParticlePair[0]->WorldSpaceInflatedBounds();
				const TAABB<FReal, 3>& Box1 = ParticlePair[1]->WorldSpaceInflatedBounds();
				if (Box0.Intersects(Box1))
				{
					// Pair array is const, the particles are not
					TGeometryParticleHandle<FReal, 3>* Particle0 = const_cast<TGeometryParticleHandle<FReal, 3>*>(ParticlePair[0]);
					TGeometryParticleHandle<FReal, 3>* Particle1 = const_cast<TGeometryParticleHandle<FReal, 3>*>(ParticlePair[1]);

					NarrowPhase.GenerateCollisions(ConstraintsArray, Dt, Particle0, Particle1, CullDistance, StatData);
				}
			}
		}

	private:
		const TArray<FParticlePair>& ParticlePairs;
		FReal CullDistance;
	};
}
