// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Convex.h"
#include "Chaos/Defines.h"
#include "Chaos/Transform.h"

namespace Chaos
{
	namespace Collisions
	{

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