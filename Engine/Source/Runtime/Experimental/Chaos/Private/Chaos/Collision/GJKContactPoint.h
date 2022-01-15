// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/CastingUtilities.h"
#include "Chaos/Core.h"
#include "Chaos/ImplicitFwd.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/GJK.h"


namespace Chaos
{
	template <typename GeometryA, typename GeometryB>
	FContactPoint GJKContactPoint2(const GeometryA& A, const GeometryB& B, const FRigidTransform3& ATM, const FRigidTransform3& BToATM, const FVec3& InitialDir, const FReal ShapePadding)
	{
		FContactPoint Contact;

		FReal Penetration;
		FVec3 ClosestA, ClosestBInA, Normal;
		int32 ClosestVertexIndexA, ClosestVertexIndexB;

		// Slightly increased epsilon to reduce error in normal for almost touching objects.
		const FReal Epsilon = 3.e-3f;

		const FReal ThicknessA = 0.5f * ShapePadding;
		const FReal ThicknessB = 0.5f * ShapePadding;
		if (GJKPenetration<true>(A, B, BToATM, Penetration, ClosestA, ClosestBInA, Normal, ClosestVertexIndexA, ClosestVertexIndexB, ThicknessA, ThicknessB, InitialDir, Epsilon))
		{
			// GJK output is all in the local space of A. We need to transform the B-relative position and the normal in to B-space
			Contact.ShapeContactPoints[0] = ClosestA;
			Contact.ShapeContactPoints[1] = BToATM.InverseTransformPosition(ClosestBInA);
			Contact.ShapeContactNormal = -BToATM.InverseTransformVector(Normal);
			Contact.Phi = -Penetration;
		}

		return Contact;
	}

	template <typename GeometryA, typename GeometryB>
	FContactPoint GJKContactPoint(const GeometryA& A, const FRigidTransform3& ATM, const GeometryB& B, const FRigidTransform3& BTM, const FVec3& InitialDir, const FReal ShapePadding)
	{
		const FRigidTransform3 BToATM = BTM.GetRelativeTransform(ATM);
		return GJKContactPoint2(A, B, ATM, BToATM, InitialDir, ShapePadding);
	}

	inline FContactPoint GenericConvexConvexContactPoint(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FReal ShapePadding)
	{
		// This expands to a switch of switches that calls the inner function with the appropriate concrete implicit types
		return Utilities::CastHelperNoUnwrap(A, ATM, [&](const auto& ADowncast, const FRigidTransform3& AFullTM)
			{
				return Utilities::CastHelperNoUnwrap(B, BTM, [&](const auto& BDowncast, const FRigidTransform3& BFullTM)
					{
						return GJKContactPoint(ADowncast, AFullTM, BDowncast, BFullTM, FVec3(1, 0, 0), ShapePadding);
					});
			});
	}
}