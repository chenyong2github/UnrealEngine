// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandleFwd.h"

namespace Chaos
{
	template <typename T, int d>
	class TSphere;

	class FCapsule;

	class FConvex;

	template <typename T, int d>
	class TPlane;

	class FCollisionContext;
	class FCollisionConstraintBase;
	class FHeightField;
	class FImplicitObject;
	class FRigidBodyPointContactConstraint;
	class FTriangleMeshImplicitObject;


	namespace Collisions
	{

		//
		// Constraint API
		//

		// Update the constraint by re-running collision detection on the shape pair.

		template<ECollisionUpdateType UpdateType, typename RigidBodyContactConstraint>
		void CHAOS_API UpdateConstraintFromGeometry(RigidBodyContactConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal Dt);

		// Create constraints for the particle pair. This could create multiple constraints: one for each potentially colliding shape pair in multi-shape particles.
		void CHAOS_API ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, const FReal dT,const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints);


		// @todo(chaos): this is only called in tests - should it really be exposed?
		template<ECollisionUpdateType UpdateType>
		void UpdateLevelsetLevelsetConstraint(const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint);

	}
}
