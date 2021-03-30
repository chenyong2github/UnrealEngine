// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Collision/SphereConvexContactPoint.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/Convex.h"
#include "Chaos/GJK.h"
#include "Chaos/GJKShape.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Sphere.h"

namespace Chaos
{
	// Calculate the shortest vector for the point to depenetrate the convex.
	// Returns true unless there are no planes in the convex
	// Note: this may be called with small positive separations of order epsilon passed to GJK,
	// but the result is increasingly inaccurate as distance increases.
	template<typename T_CONVEX>
	bool ConvexPointPenetrationVector(const T_CONVEX& Convex, const FVec3& X, FVec3& OutNormal, FReal& OutPhi)
	{
		FReal MaxPhi = TNumericLimits<FReal>::Lowest();
		int32 MaxPlaneIndex = INDEX_NONE;

		const int32 NumPlanes = Convex.NumPlanes();
		for (int32 PlaneIndex = 0; PlaneIndex < NumPlanes; ++PlaneIndex)
		{
			const FReal Phi = Convex.GetPlane(PlaneIndex).SignedDistance(X);
			if (Phi > MaxPhi)
			{
				MaxPhi = Phi;
				MaxPlaneIndex = PlaneIndex;
			}
		}

		if (MaxPlaneIndex != INDEX_NONE)
		{
			OutPhi = MaxPhi;
			OutNormal = Convex.GetPlane(MaxPlaneIndex).Normal();
			return true;
		}

		return false;
	}


	// Use GJK (point to convex) to calculate separation.
	// Fall back to plane testing if penetrating by more than Radius.
	template<typename T_CONVEX>
	FContactPoint ConvexSphereContactPointImpl(const T_CONVEX& Convex, const FRigidTransform3& ConvexTransform, const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform)
	{
		FContactPoint ContactPoint;
		if (Convex.NumPlanes() > 0)
		{
			const FRigidTransform3 SphereToConvexTransform = SphereTransform.GetRelativeTransform(ConvexTransform);
			FVec3 PosConvex, PosSphere, NormalConvex;
			FReal Phi;

			// Run GJK to find separating distance if available
			// NOTE: Sphere is treated as a point (its core shape), Convex margin is ignored so we are using the outer non-shrunken hull.
			const EGJKDistanceResult GjkResult = GJKDistance(MakeGJKShape(Convex), MakeGJKCoreShape(Sphere), SphereToConvexTransform, Phi, PosConvex, PosSphere, NormalConvex);
			bool bHaveResult = (GjkResult != EGJKDistanceResult::DeepContact);

			// If GJK failed, core shapes are penetrating so find the minimum penetration vector
			if (!bHaveResult)
			{
				const FVec3 SpherePosConvex = SphereToConvexTransform.TransformPosition(Sphere.GetCenter());

				FReal PointPhi;
				bHaveResult = ConvexPointPenetrationVector(Convex, SpherePosConvex, NormalConvex, PointPhi);
				if (bHaveResult)
				{
					PosConvex = SpherePosConvex - PointPhi * NormalConvex;
					PosSphere = SphereToConvexTransform.InverseTransformVector(-Sphere.GetRadius() * NormalConvex);
					Phi = PointPhi - Sphere.GetRadius();
				}
			}

			// Build the contact point
			if (bHaveResult)
			{
				ContactPoint.ShapeContactPoints[0] = PosConvex;
				ContactPoint.ShapeContactPoints[1] = PosSphere;
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
	FContactPoint SphereConvexContactPointImpl(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const T_CONVEX& Convex, const FRigidTransform3& ConvexTransform)
	{
		return ConvexSphereContactPointImpl(Convex, ConvexTransform, Sphere, SphereTransform).SwapShapes();
	}

	FContactPoint SphereConvexContactPoint(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const FImplicitConvex3& Convex, const FRigidTransform3& ConvexTransform)
	{
		return SphereConvexContactPointImpl(Sphere, SphereTransform, Convex, ConvexTransform);
	}

	FContactPoint SphereConvexContactPoint(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const TImplicitObjectInstanced<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform)
	{
		return SphereConvexContactPointImpl(Sphere, SphereTransform, Convex, ConvexTransform);
	}

	FContactPoint SphereConvexContactPoint(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const TImplicitObjectScaled<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform)
	{
		return SphereConvexContactPointImpl(Sphere, SphereTransform, Convex, ConvexTransform);
	}



	FContactPoint ConvexSphereContactPoint(const FImplicitConvex3& Convex, const FRigidTransform3& ConvexTransform, const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform)
	{
		return ConvexSphereContactPointImpl(Convex, ConvexTransform, Sphere, SphereTransform);
	}

	FContactPoint ConvexSphereContactPoint(const TImplicitObjectInstanced<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform, const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform)
	{
		return ConvexSphereContactPointImpl(Convex, ConvexTransform, Sphere, SphereTransform);
	}

	FContactPoint ConvexSphereContactPoint(const TImplicitObjectScaled<FImplicitConvex3>& Convex, const FRigidTransform3& ConvexTransform, const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform)
	{
		return ConvexSphereContactPointImpl(Convex, ConvexTransform, Sphere, SphereTransform);
	}



	FContactPoint SphereConvexContactPoint(const FImplicitSphere3& Sphere, const FRigidTransform3& SphereTransform, const FImplicitObject& Object, const FRigidTransform3& ConvexTransform)
	{
		if (const TImplicitObjectInstanced<FImplicitConvex3>* InstancedConvex = Object.template GetObject<const TImplicitObjectInstanced<FImplicitConvex3>>())
		{
			return SphereConvexContactPoint(Sphere, SphereTransform, *InstancedConvex, ConvexTransform);
		}
		else if (const TImplicitObjectScaled<FImplicitConvex3>* ScaledConvex = Object.template GetObject<const TImplicitObjectScaled<FImplicitConvex3>>())
		{
			return SphereConvexContactPoint(Sphere, SphereTransform, *ScaledConvex, ConvexTransform);
		}
		else if (const FImplicitConvex3* Convex = Object.template GetObject<const FImplicitConvex3>())
		{
			return SphereConvexContactPoint(Sphere, SphereTransform, *Convex, ConvexTransform);
		}
		return FContactPoint();
	}

}
