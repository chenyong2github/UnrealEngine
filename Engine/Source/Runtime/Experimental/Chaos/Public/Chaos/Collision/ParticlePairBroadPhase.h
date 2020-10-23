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
	 * In addition, collide all particles in ParticlesA with all particles in ParticlesB.
	 *
	 * No spatial acceleration, and the order is assumed to be already optimized for cache efficiency.
	 */
	class CHAOS_API FParticlePairBroadPhase
	{
	public:
		using FParticleHandle = TGeometryParticleHandle<FReal, 3>;
		using FParticlePair = TVector<FParticleHandle*, 2>;
		using FAABB = TAABB<FReal, 3>;

		FParticlePairBroadPhase(const TArray<FParticlePair>* InParticlePairs, const TArray<FParticleHandle*>* InParticlesA, const TArray<FParticleHandle*>* InParticlesB, const FReal InCullDistance)
			: ParticlePairs(InParticlePairs)
			, ParticlesA(InParticlesA)
			, ParticlesB(InParticlesB)
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

			if (ParticlePairs != nullptr)
			{
				for (const FParticlePair& ParticlePair : *ParticlePairs)
				{
					// Pair array is const and the particles are not, but we need to const_cast
					FParticleHandle* ParticleA = const_cast<FParticleHandle*>(ParticlePair[0]);
					FParticleHandle* ParticleB = const_cast<FParticleHandle*>(ParticlePair[1]);

					if ((ParticleA != nullptr) && (ParticleB != nullptr))
					{
						ProduceOverlaps(Dt, ConstraintsArray, NarrowPhase, ParticleA, ParticleB, StatData);
					}
				}
			}

			if ((ParticlesA != nullptr) && (ParticlesB != nullptr))
			{
				for (FParticleHandle* ParticleA : *ParticlesA)
				{
					if (ParticleA != nullptr)
					{
						for (FParticleHandle* ParticleB : *ParticlesB)
						{
							if (ParticleB != nullptr)
							{
								ProduceOverlaps(Dt, ConstraintsArray, NarrowPhase, ParticleA, ParticleB, StatData);
							}
						}
					}
				}
			}
		}

	private:
		inline void ProduceOverlaps(
			FReal Dt,
			FCollisionConstraintsArray& ConstraintsArray,
			FNarrowPhase& NarrowPhase,
			FParticleHandle* ParticleA,
			FParticleHandle* ParticleB,
			CollisionStats::FStatData& StatData)
		{
			const TAABB<FReal, 3>& Box0 = ParticleA->WorldSpaceInflatedBounds();
			const TAABB<FReal, 3>& Box1 = ParticleB->WorldSpaceInflatedBounds();
			if (Box0.Intersects(Box1))
			{
				NarrowPhase.GenerateCollisions(ConstraintsArray, Dt, ParticleA, ParticleB, CullDistance);
			}

			CHAOS_COLLISION_STAT(if (ConstraintsArray.Num()) { StatData.IncrementCountNP(ConstraintsArray.Num()); });
			CHAOS_COLLISION_STAT(if (!ConstraintsArray.Num()) { StatData.IncrementRejectedNP(); });
		}

		const TArray<FParticlePair>* ParticlePairs;
		const TArray<FParticleHandle*>* ParticlesA;
		const TArray<FParticleHandle*>* ParticlesB;
		FReal CullDistance;
	};
}
