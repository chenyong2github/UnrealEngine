// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandleFwd.h"


extern bool CCDUseInitialRotationForSweptUpdate;

namespace Chaos
{
	template <typename T, int d>
	class TSphere;

	class FCapsule;

	class FConvex;

	template <typename T, int d>
	class TPlane;

	class FCollisionContext;
	class FHeightField;
	class FImplicitObject;
	class FPBDCollisionConstraint;
	class FTriangleMeshImplicitObject;


	namespace Collisions
	{

		//
		// Constraint API
		//

		// Update the constraint by re-running collision detection on the shape pair.
		template<ECollisionUpdateType UpdateType>
		void CHAOS_API UpdateConstraintFromGeometry(FPBDCollisionConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal Dt);

		// Update the constraint by re-running collision detection on the shape pair.
		// Return whether CCD is needed for this constraint.
		template<ECollisionUpdateType UpdateType>
		bool CHAOS_API UpdateConstraintFromGeometrySwept(FPBDCollisionConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal Dt);

		// Create constraints for the particle pair. This could create multiple constraints: one for each potentially colliding shape pair in multi-shape particles.
		void CHAOS_API ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1, const FRigidTransform3& ParticleWorldTransform0, const FRigidTransform3& Transform0, const FRigidTransform3& ParticleWorldTransform1, const FRigidTransform3& Transform1, const FReal CullDistance, const FReal Dt,const FCollisionContext& Context);

		// Attempt to restore all constraints for the particle pair. This will fail is the pair were not previously colliding, or if their relative transforms have changed. Return true if constraints were restored.
		bool CHAOS_API TryRestoreConstraints(FConstGenericParticleHandle Particle0, FConstGenericParticleHandle Particle1, const FRigidTransform3& ParticleWorldTransform0, const FRigidTransform3& ParticleWorldTransform1, const FReal Dt, const FCollisionContext& Context);


		// @todo(chaos): this is only called in tests - should it really be exposed?
		template<ECollisionUpdateType UpdateType>
		void UpdateLevelsetLevelsetConstraint(const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal Dt, FPBDCollisionConstraint& Constraint);

		// Reset per-frame collision stat counters
		void CHAOS_API ResetChaosCollisionCounters();
	}
}
