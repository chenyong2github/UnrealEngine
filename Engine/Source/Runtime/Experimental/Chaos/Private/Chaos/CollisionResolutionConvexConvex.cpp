// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolutionConvexConvex.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"

namespace Chaos
{

	template<class T, int d>
	void CollisionResolutionConvexConvex<T, d>::ConstructConvexConvexConstraints(
		FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1,
		const FImplicitObject* Implicit0, const FImplicitObject* Implicit1,
		const float Thickness,
		FPointContactConstraint& Constraint)
	{
		if (Constraint.ContainsManifold(Implicit0, Implicit1))
		{
			return;
		}

		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	template<class T, int d>
	void CollisionResolutionConvexConvex<T, d>::UpdateConvexConvexConstraint(
		const FImplicitObject& A, const FRigidTransform& ATM,
		const FImplicitObject& B, FRigidTransform BTM,
		float Thickness,
		FPointContactConstraint& Constraint)
	{
		TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);

		if (ensure(GetInnerType(A.GetType()) == ImplicitObjectType::Convex && GetInnerType(B.GetType()) == ImplicitObjectType::Convex))
		{
			const TConvex<T, d>* AObject = nullptr;
			const TConvex<T, d>* BObject = nullptr;

			if (IsScaled(A.GetType()) && IsScaled(B.GetType()))
			{
				AObject = static_cast<const TConvex<T, d>*>(static_cast<const TImplicitObjectScaled<TConvex<T, d>>&>(A).Object().Get());
				BObject = static_cast<const TConvex<T, d>*>(static_cast<const TImplicitObjectScaled<TConvex<T, d>>&>(B).Object().Get());
			}
			else if (A.GetType() == ImplicitObjectType::Transformed && B.GetType() == ImplicitObjectType::Transformed)
			{
				AObject = static_cast<const TConvex<T, d>*>(static_cast<const TImplicitObjectTransformed<T, d>&>(A).Object().Get());
				BObject = static_cast<const TConvex<T, d>*>(static_cast<const TImplicitObjectTransformed<T, d>&>(B).Object().Get());
			}

			if (AObject && BObject)
			{
				const TParticles<T, d>& SurfaceParticles = AObject->GetSurfaceParticles();

				Constraint.SetDisabled(true);

				if (GJKIntersection(*AObject, *BObject, BToATM, Thickness))
				{
					TRigidTransform<T, d> AToBTM = ATM.GetRelativeTransform(BTM);

					T Phi;
					TVector<T, d> Normal;
					for (int32 Idx = 0; Idx < (int32)SurfaceParticles.Size(); Idx++)
					{
						Phi = BObject->PhiWithNormal(AToBTM.TransformPosition(SurfaceParticles.X(Idx)), Normal);
						if (Phi < Constraint.GetPhi())
						{
							Constraint.SetPhi(Phi);
							Constraint.SetLocation(ATM.TransformPosition(SurfaceParticles.X(Idx)));
							Constraint.SetNormal(BTM.TransformVector(Normal));
						}
					}
					return;
				}
				return;
			}
		}

		ensureMsgf(false, TEXT("Unsupported convex to convex constraint."));
	}

	template class CollisionResolutionConvexConvex<float, 3>;

}
