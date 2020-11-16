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
	#define SCOPE_CYCLE_COUNTER_NAROWPHASE() SCOPE_CYCLE_COUNTER(STAT_Collisions_NarrowPhase)
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
		void GenerateCollisions(FCollisionConstraintsArray& NewConstraints, FReal Dt, TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FReal CullDistance)
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

				// @todo(mlentine): Collision particles should exist optionally on geometry particles not rigid dynamic particles
				TPBDRigidParticleHandle<FReal, 3>* RigidParticle0 = Particle0->CastToRigidParticle();
				TPBDRigidParticleHandle<FReal, 3>* RigidParticle1 = Particle1->CastToRigidParticle();
				Collisions::ConstructConstraints(Particle0, Particle1, Particle0->Geometry().Get(), RigidParticle0 ? RigidParticle0->CollisionParticles().Get() : nullptr, Particle1->Geometry().Get(), RigidParticle1 ? RigidParticle1->CollisionParticles().Get() : nullptr, FRigidTransform3(), FRigidTransform3(), CullDistance, Dt, Context, NewConstraints);
			}
		}

	private:
		FCollisionContext Context;
	};
}
