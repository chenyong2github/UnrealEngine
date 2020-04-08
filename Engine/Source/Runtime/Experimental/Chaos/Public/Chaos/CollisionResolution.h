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

	template <typename T>
	class TCapsule;

	class FConvex;

	template <typename T, int d>
	class TPlane;

	class FCollisionContext;
	class FCollisionConstraintBase;
	class FHeightField;
	class FImplicitObject;
	class FRigidBodyPointContactConstraint;
	class FRigidBodyMultiPointContactConstraint;
	class FTriangleMeshImplicitObject;


	namespace Collisions
	{

		//
		// Constraint API
		//

		// Build a contact manifold for the shape pair. The manifold is a plane attached to one shape, and points attached to the other.
		void CHAOS_API UpdateManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance, const FCollisionContext& Context);

		// Update the constraint using the pre-built manifold, finding the manifold point that is most deeply penetrating the manifold plane.
		void CHAOS_API UpdateConstraintFromManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance);

		// Update the constraint by re-running collision detection on the shape pair.
		template<ECollisionUpdateType UpdateType>
		void CHAOS_API UpdateConstraintFromGeometry(FRigidBodyPointContactConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal CullDistance);

		// Create constraints for the particle pair. This could create multiple constraints: one for each potentially colliding shape pair in multi-shape particles.
		void CHAOS_API ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints);


		// @todo(chaos): this is only called in tests - should it really be exposed?
		template<ECollisionUpdateType UpdateType>
		void CHAOS_API UpdateLevelsetLevelsetConstraint(const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

	}
}
