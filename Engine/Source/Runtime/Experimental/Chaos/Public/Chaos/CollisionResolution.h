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

	template <typename T>
	class THeightField;

	class FCollisionContext;
	class FCollisionConstraintBase;
	class FImplicitObject;
	class FRigidBodyPointContactConstraint;
	class FRigidBodyMultiPointContactConstraint;
	class FTriangleMeshImplicitObject;


	namespace Collisions
	{

		//
		// Box-Box
		//

		void CHAOS_API UpdateBoxBoxConstraint(const FAABB3& Box1, const FRigidTransform3& Box1Transform, const FAABB3& Box2, const FRigidTransform3& Box2Transform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateBoxBoxManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructBoxBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);


		//
		// Box-HeightField
		//

		void CHAOS_API UpdateBoxHeightFieldConstraint(const FAABB3& A, const FRigidTransform3& ATransform, const THeightField<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateBoxHeightFieldManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructBoxHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Box-Plane
		//

		void CHAOS_API UpdateBoxPlaneConstraint(const FAABB3& Box, const FRigidTransform3& BoxTransform, const TPlane<FReal, 3>& Plane, const FRigidTransform3& PlaneTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateBoxPlaneManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructBoxPlaneConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);



		//
		// Box-TriangleMesh
		//

		template <typename TriMeshType>
		void CHAOS_API UpdateBoxTriangleMeshConstraint(const FAABB3& Box0, const FRigidTransform3& Transform0, const TriMeshType & TriangleMesh1, const FRigidTransform3& Transformw1, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateBoxTriangleMeshManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructBoxTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);


		//
		// Sphere-Sphere
		//

		void CHAOS_API UpdateSphereSphereConstraint(const TSphere<FReal, 3>& Sphere1, const FRigidTransform3& Sphere1Transform, const TSphere<FReal, 3>& Sphere2, const FRigidTransform3& Sphere2Transform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateSphereSphereManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructSphereSphereConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Sphere-HeightField
		//

		void CHAOS_API UpdateSphereHeightFieldConstraint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const THeightField<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateSphereHeightFieldManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructSphereHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Sphere-Plane
		//

		void CHAOS_API UpdateSpherePlaneConstraint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const TPlane<FReal, 3>& Plane, const FRigidTransform3& PlaneTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateSpherePlaneManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructSpherePlaneConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Sphere-Box
		//

		void CHAOS_API UpdateSphereBoxConstraint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FAABB3& Box, const FRigidTransform3& BoxTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateSphereBoxManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructSphereBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Sphere-Capsule
		//

		void CHAOS_API UpdateSphereCapsuleConstraint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const TCapsule<FReal>& Box, const FRigidTransform3& BoxTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateSphereCapsuleManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructSphereCapsuleConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Capsule-Capsule
		//

		void CHAOS_API UpdateCapsuleCapsuleConstraint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const TCapsule<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateCapsuleCapsuleManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Capsule-Box
		//

		void CHAOS_API UpdateCapsuleBoxConstraint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const FAABB3& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API ConstructCapsuleBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints);

		//
		// Capsule-HeightField
		//

		void CHAOS_API UpdateCapsuleHeightFieldConstraint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const THeightField<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateCapsuleHeightFieldManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void ConstructCapsuleHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Convex-Convex
		//
		void CHAOS_API UpdateConvexConvexConstraint(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FReal CullDistance, FCollisionConstraintBase& Constraint);

		void CHAOS_API UpdateConvexConvexManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructConvexConvexConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);

		//
		// Convex-HeightField
		//

		void CHAOS_API UpdateConvexHeightFieldConstraint(const FImplicitObject& A, const FRigidTransform3& ATransform, const THeightField<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateConvexHeightFieldManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructConvexHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);


		//
		// Levelset-Levelset
		//
		template<ECollisionUpdateType UpdateType>
		void CHAOS_API UpdateLevelsetLevelsetConstraint(const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint);

		void CHAOS_API UpdateLevelsetLevelsetManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance);

		void CHAOS_API ConstructLevelsetLevelsetConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints);


		//
		// Constraint API
		//
		void CHAOS_API UpdateSingleShotManifold(FRigidBodyMultiPointContactConstraint&  Constraint, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance);

		void CHAOS_API UpdateIterativeManifold(FRigidBodyMultiPointContactConstraint&  Constraint, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance);

		void CHAOS_API UpdateManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance, const FCollisionContext& Context);

		void UpdateConstraintFromManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance);

		template<ECollisionUpdateType UpdateType>
		void UpdateConstraintFromGeometry(FRigidBodyPointContactConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal CullDistance);

		void CHAOS_API ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints);

	}
}
