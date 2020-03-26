// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/StatsData.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosStats.h"

namespace Chaos
{
#define CHAOS_ENABLE_STAT_NARROWPHASE 0
#if CHAOS_ENABLE_STAT_NARROWPHASE
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::NarrowPhase"), STAT_Collisions_NarrowPhase, STATGROUP_ChaosCollision, CHAOS_API);
	#define SCOPE_CYCLE_COUNTER_NAROWPHASE() SCOPE_CYCLE_COUNTER(STAT_Collisions_GJK)
#else
	#define SCOPE_CYCLE_COUNTER_NAROWPHASE()
#endif

	class FCollisionContext;

	/**
	 * Generate contact manifolds for particle pairs.
	 *
	 * /see FAsyncCollisionReceiver, FSyncCollisionReceiver.
	 */
	class CHAOS_API FNarrowPhase
	{
	public:
		FNarrowPhase()
		{
		}

		FCollisionContext& GetContext() { return Context; }

		// @todo(chaos): COLLISION Transient Handle version
		/**
		 * /param CullDistance The contact separation at which we ignore the constraint
		 */
		void GenerateCollisions(FCollisionConstraintsArray& NewConstraints, FReal Dt, TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FReal CullDistance, CollisionStats::FStatData& StatData)
		{
			SCOPE_CYCLE_COUNTER_NAROWPHASE();
			if (ensure(Particle0 && Particle1))
			{
				//
				// @todo(chaos) : Collision Constraints
				//   This is not efficient. The constraint has to go through a construction 
				//   process, only to be deleted later if it already existed. This should 
				//   determine if the constraint is already defined, and then opt out of 
				//   the creation process. 
				//
				Collisions::ConstructConstraints(Particle0, Particle1, Particle0->Geometry().Get(), Particle1->Geometry().Get(), FRigidTransform3(), FRigidTransform3(), CullDistance, Context, NewConstraints);

				CHAOS_COLLISION_STAT(if (NewConstraints.Num()) { StatData.IncrementCountNP(NewConstraints.Num()); });
				CHAOS_COLLISION_STAT(if (!NewConstraints.Num()) { StatData.IncrementRejectedNP(); });
			}
		}

	private:
		FCollisionContext Context;
	};
}
