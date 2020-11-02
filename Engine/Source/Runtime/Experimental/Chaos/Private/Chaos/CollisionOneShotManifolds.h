// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Convex.h"
#include "Chaos/Defines.h"
#include "Chaos/Transform.h"

namespace Chaos
{
	namespace Collisions
	{
		uint32 BoxBoxClipVerticesAgainstPlane(const FVec3* InputVertexBuffer, FVec3* outputVertexBuffer, uint32 ClipPointCount, FReal ClippingAxis, FReal Distance);
		uint32 ReduceManifoldContactPoints(FVec3* Points, uint32 PointCount);

		void ConstructBoxBoxOneShotManifold(
			const FImplicitBox3& Box1,
			const FRigidTransform3& Box1Transform, //world
			const FImplicitBox3& Box2,
			const FRigidTransform3& Box2Transform, //world
			const FReal CullDistance,
			FRigidBodyPointContactConstraint& Constraint,
			bool bInInitialize);

		template <typename ConvexImplicitType>
		void ConstructConvexConvexOneShotManifold(
			const ConvexImplicitType& Implicit1,
			const FRigidTransform3& Convex1Transform, //world
			const ConvexImplicitType& Implicit2,
			const FRigidTransform3& Convex2Transform, //world
			const FReal CullDistance,
			FRigidBodyPointContactConstraint& Constraint,
			bool bInInitialize);
	}
}