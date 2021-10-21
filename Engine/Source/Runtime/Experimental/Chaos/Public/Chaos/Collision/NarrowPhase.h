// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/StatsData.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"
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
	 */
	class CHAOS_API FNarrowPhase
	{
	public:
		FNarrowPhase()
		{
		}

		FCollisionContext& GetContext() { return Context; }

		/**
		 * /param CullDistance The contact separation at which we ignore the constraint
		 */
		void GenerateCollisions(FReal Dt, TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FReal CullDistance, const bool bForceDisableCCD)
		{
			SCOPE_CYCLE_COUNTER_NAROWPHASE();
			if (ensure(Particle0 && Particle1))
			{
				FConstGenericParticleHandle P0 = Particle0;
				FConstGenericParticleHandle P1 = Particle1;
				const FRigidTransform3 ParticleTransform0 = FParticleUtilitiesPQ::GetActorWorldTransform(P0);
				const FRigidTransform3 ParticleTransform1 = FParticleUtilitiesPQ::GetActorWorldTransform(P1);

				Context.bForceDisableCCD = bForceDisableCCD;

				// @todo(mlentine): Collision particles should exist optionally on geometry particles not rigid dynamic particles
				Collisions::ConstructConstraints(
					Particle0, 
					Particle1, 
					Particle0->Geometry().Get(), 
					P0->CollisionParticles().Get(), 
					Particle1->Geometry().Get(), 
					P1->CollisionParticles().Get(), 
					ParticleTransform0, 
					FRigidTransform3(), 
					ParticleTransform1, 
					FRigidTransform3(), 
					CullDistance, 
					Dt, 
					Context);
			}
		}

		bool TryRestoreCollisions(FReal Dt, TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1)
		{
			FConstGenericParticleHandle P0 = Particle0;
			FConstGenericParticleHandle P1 = Particle1;
			const FRigidTransform3 ParticleTransform0 = FParticleUtilitiesPQ::GetActorWorldTransform(P0);
			const FRigidTransform3 ParticleTransform1 = FParticleUtilitiesPQ::GetActorWorldTransform(P1);

			return Collisions::TryRestoreConstraints(
				P0,
				P1,
				ParticleTransform0,
				ParticleTransform1,
				Dt,
				Context);
		}

	private:
		FCollisionContext Context;
	};
}
