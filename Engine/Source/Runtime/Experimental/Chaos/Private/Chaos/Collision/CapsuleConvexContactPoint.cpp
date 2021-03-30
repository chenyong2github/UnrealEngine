// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/CapsuleConvexContactPoint.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/GJK.h"
#include "Chaos/GJKShape.h"
#include "Chaos/ImplicitObjectScaled.h"

namespace Chaos
{
	// Use GJK (point to convex) to calculate separation.
	// Fall back to plane testing if penetrating by more than Radius.
	template<typename T_CONVEX>
	FContactPoint ConvexCapsuleContactPointImpl(const T_CONVEX& Convex, const FRigidTransform3& ConvexTransform, const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform)
	{
		FContactPoint ContactPoint;
		if (Convex.NumPlanes() > 0)
		{
			const FRigidTransform3 CapsuleToConvexTransform = CapsuleTransform.GetRelativeTransform(ConvexTransform);
			FVec3 PosConvex, PosCapsuleInConvex, NormalConvex;
			FReal Penetration;
			int32 VertexIndexA, VertexIndexB;

			// Run GJK to find separating distance if available
			// NOTE: Capsule is treated as a line (its core shape), Convex margin is ignored so we are using the outer non-shrunken hull.
			// @todo(chaos): use GJKDistance and SAT when that fails rather that GJK/EPA (but this requires an edge list in the convex)
			bool bHaveResult = GJKPenetration<true>(MakeGJKShape(Convex), MakeGJKCoreShape(Capsule), CapsuleToConvexTransform, Penetration, PosConvex, PosCapsuleInConvex, NormalConvex, VertexIndexA, VertexIndexB);
			
			// Build the contact point
			if (bHaveResult)
			{
				const FVec3 PosCapsule = CapsuleToConvexTransform.InverseTransformPosition(PosCapsuleInConvex);
				const FReal Phi = -Penetration;

				ContactPoint.ShapeContactPoints[0] = PosConvex;
				ContactPoint.ShapeContactPoints[1] = PosCapsule;
				ContactPoint.ShapeContactNormal = -NormalConvex;
				ContactPoint.ShapeMargins[0] = 0.0f;
				ContactPoint.ShapeMargins[1] = 0.0f;
				ContactPoint.ContactNormalOwnerIndex = 0;

				ContactPoint.Location = ConvexTransform.TransformPosition(ContactPoint.ShapeContactPoints[0]);
				ContactPoint.Normal = ConvexTransform.TransformVectorNoScale(-NormalConvex);
				ContactPoint.Phi = Phi;
			}
		}

		return ContactPoint;
	}

	template<typename T_CONVEX>
	FContactPoint CapsuleConvexContactPointImpl(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const T_CONVEX& Convex, const FRigidTransform3& ConvexTransform)
	{
		return ConvexCapsuleContactPointImpl(Convex, ConvexTransform, Capsule, CapsuleTransform).SwapShapes();
	}



	FContactPoint CapsuleConvexContactPoint(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const FImplicitConvex3& Convex, const FRigidTransform3& ConvexTransform)
	{
		return CapsuleConvexContactPointImpl(Capsule, CapsuleTransform, Convex, ConvexTransform);
	}

	FContactPoint CapsuleConvexContactPoint(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const TImplicitObjectInstanced<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform)
	{
		return CapsuleConvexContactPointImpl(Capsule, CapsuleTransform, Convex, ConvexTransform);
	}

	FContactPoint CapsuleConvexContactPoint(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const TImplicitObjectScaled<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform)
	{
		return CapsuleConvexContactPointImpl(Capsule, CapsuleTransform, Convex, ConvexTransform);
	}



	FContactPoint ConvexCapsuleContactPoint(const FImplicitConvex3& Convex, const FRigidTransform3& ConvexTransform, const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform)
	{
		return ConvexCapsuleContactPointImpl(Convex, ConvexTransform, Capsule, CapsuleTransform);
	}

	FContactPoint ConvexCapsuleContactPoint(const TImplicitObjectInstanced<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform, const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform)
	{
		return ConvexCapsuleContactPointImpl(Convex, ConvexTransform, Capsule, CapsuleTransform);
	}

	FContactPoint ConvexCapsuleContactPoint(const TImplicitObjectScaled<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform, const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform)
	{
		return ConvexCapsuleContactPointImpl(Convex, ConvexTransform, Capsule, CapsuleTransform);
	}



	FContactPoint CapsuleConvexContactPoint(const FImplicitCapsule3& Capsule, const FRigidTransform3& CapsuleTransform, const FImplicitObject& Object, const FRigidTransform3& ConvexTransform)
	{
		if (const TImplicitObjectInstanced<FImplicitConvex3>* InstancedConvex = Object.template GetObject<const TImplicitObjectInstanced<FImplicitConvex3>>())
		{
			return CapsuleConvexContactPoint(Capsule, CapsuleTransform, *InstancedConvex, ConvexTransform);
		}
		else if (const TImplicitObjectScaled<FImplicitConvex3>* ScaledConvex = Object.template GetObject<const TImplicitObjectScaled<FImplicitConvex3>>())
		{
			return CapsuleConvexContactPoint(Capsule, CapsuleTransform, *ScaledConvex, ConvexTransform);
		}
		else if (const FImplicitConvex3* Convex = Object.template GetObject<const FImplicitConvex3>())
		{
			return CapsuleConvexContactPoint(Capsule, CapsuleTransform, *Convex, ConvexTransform);
		}
		return FContactPoint();
	}

}
