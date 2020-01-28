// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/StatsData.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosStats.h"

namespace Chaos
{
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::NarrowPhase"), STAT_Collisions_NarrowPhase, STATGROUP_ChaosCollision, CHAOS_API);

	/**
	 * Generate contact manifolds for particle pairs.
	 *
	 * /see FAsyncCollisionReceiver, FSyncCollisionReceiver.
	 */
	class CHAOS_API FNarrowPhase
	{
	public:
		// @todo(chaos): COLLISION Transient Handle version
		/**
		 * /param CullDistance The contact separation at which we ignore the constraint
		 */
		void GenerateCollisions(FCollisionConstraintsArray& NewConstraints, FReal Dt, TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FReal CullDistance, CollisionStats::FStatData& StatData)
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_NarrowPhase);
			if (ensure(Particle0 && Particle1))
			{
				//
				// @todo(chaos) : Collision Constraints
				//   This is not efficient. The constraint has to go through a construction 
				//   process, only to be deleted later if it already existed. This should 
				//   determine if the constraint is already defined, and then opt out of 
				//   the creation process. 
				//
				Collisions::ConstructConstraints<FReal, 3>(Particle0, Particle1, Particle0->Geometry().Get(), Particle1->Geometry().Get(), Collisions::GetTransform(Particle0), Collisions::GetTransform(Particle1), CullDistance, NewConstraints);

				CHAOS_COLLISION_STAT(if (NewConstraints.Num()) { StatData.IncrementCountNP(NewConstraints.Num()); });
				CHAOS_COLLISION_STAT(if (!NewConstraints.Num()) { StatData.IncrementRejectedNP(); });
			}
		}
	};
}
