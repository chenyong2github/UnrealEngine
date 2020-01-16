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
	 * Generate contact manifolds for particle pairs and pass them on to the consumer. Can be composed with a
	 * multi-threaded BroadPhase as long as the collisions receiver type can handle multi-threaded calls to ReceiveCollisions.
	 *
	 * /template T_RECEIVER The object type that will take the detected collisions. Generally this will depend on the type of BroadPhase.
	 * /see FAsyncCollisionReceiver, FSyncCollisionReceiver.
	 */
	class CHAOS_API FNarrowPhase
	{
	public:
		using FCollisionConstraintsArray = TArray<TCollisionConstraintBase<FReal, 3>*>;

		// @todo(ccaulfield): COLLISION Transient Handle version
		template<typename T_RECEIVER>
		void GenerateCollisions(FReal Dt, T_RECEIVER& Receiver, TGeometryParticleHandle<FReal, 3>* Particle1, TGeometryParticleHandle<FReal, 3>* Particle2, const FReal BoundsThickness, CollisionStats::FStatData& StatData)
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_NarrowPhase);

			// @todo(ccaulfield): COLLISION - Thickness: add shape padding (BoundsThickness is the distance within which we speculatively create constraints)

			FCollisionConstraintsArray NewConstraints;
			ConstructConstraints(Particle1, Particle2, BoundsThickness, NewConstraints, StatData);

			Receiver.ReceiveCollisions(NewConstraints);
		}

	private:
		void ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FReal Thickness, FCollisionConstraintsArray& NewConstraints, CollisionStats::FStatData& StatData)
		{
			if (ensure(Particle0 && Particle1))
			{
				//
				// @todo(chaos) : Collision Constraints
				//   This is not efficient. The constraint has to go through a construction 
				//   process, only to be deleted later if it already existed. This should 
				//   determine if the constraint is already defined, and then opt out of 
				//   the creation process. 
				//
				Collisions::ConstructConstraints<FReal, 3>(Particle0, Particle1, Particle0->Geometry().Get(), Particle1->Geometry().Get(), Collisions::GetTransform(Particle0), Collisions::GetTransform(Particle1), Thickness, NewConstraints);

				CHAOS_COLLISION_STAT(if (NewConstraints.Num()) { StatData.IncrementCountNP(NewConstraints.Num()); });
				CHAOS_COLLISION_STAT(if (!NewConstraints.Num()) { StatData.IncrementRejectedNP(); });
			}
		}
	};
}
