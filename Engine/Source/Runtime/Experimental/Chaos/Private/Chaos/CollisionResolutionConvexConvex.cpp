// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolutionConvexConvex.h"
#include "Chaos/ImplicitObjectScaled.h"

namespace Chaos
{

	template<class T, int d>
	void CollisionResolutionConvexConvex<T, d>::ConstructConvexConvexConstraints(
		FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1,
		const FImplicitObject* Implicit0, const FImplicitObject* Implicit1,
		const float Thickness,
		TArray<FRigidBodyContactConstraint>& ConstraintBuffer,
		FCollisionResolutionManifold* Manifold,
		float ManifoldScale, int32 ManifoldSamples)
	{
		if (Manifold)
		{
			if (Manifold->ContainsShapeConnection(Implicit0, Implicit1))
			{
				return;
			}
		}

		TRigidBodyContactConstraint<T, d> Constraint;
		Constraint.Particle = Particle0;
		Constraint.Levelset = Particle1;
		Constraint.Geometry[0] = Implicit0;
		Constraint.Geometry[1] = Implicit1;

		ConstraintBuffer.Add(Constraint);
	}

	template<class T, int d>
	void CollisionResolutionConvexConvex<T, d>::UpdateConvexConvexConstraint(
		const FImplicitObject& A, const FRigidTransform& ATM,
		const FImplicitObject& B, FRigidTransform BTM,
		float Thickness,
		FRigidBodyContactConstraint* Constraints, int32 NumConstraints,
		FCollisionResolutionManifold* Manifold,
		float ManifoldScale, int32 ManifoldSamples)
	{
		ensure(NumConstraints);

		if (ensure(IsScaled(A.GetType()) && IsScaled(B.GetType())))
		{
			TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);

			if (ensure(GetInnerType(A.GetType()) == ImplicitObjectType::Convex && GetInnerType(B.GetType()) == ImplicitObjectType::Convex))
			{
				const TConvex<T, d>* AObject = static_cast<const TConvex<T, d>*>(static_cast<const TImplicitObjectScaled<TConvex<T, d>>&>(A).Object().Get());
				const TConvex<T, d>* BObject = static_cast<const TConvex<T, d>*>(static_cast<const TImplicitObjectScaled<TConvex<T, d>>&>(B).Object().Get());
				if (AObject && BObject)
				{
					const TParticles<T, d>& SurfaceParticles = AObject->GetSurfaceParticles();

					for (int32 Idx = 0; Idx < NumConstraints; Idx++)
					{
						Constraints[Idx].bDisabled = true;
					}

					if (GJKIntersection(*AObject, *BObject, BToATM, Thickness))
					{
						TRigidTransform<T, d> AToBTM = ATM.GetRelativeTransform(BTM);

						T Phi;
						TVector<T, d> Normal;
						for (int32 Idx = 0; Idx < (int32)SurfaceParticles.Size(); Idx++)
						{
							Phi = BObject->PhiWithNormal(AToBTM.TransformPosition(SurfaceParticles.X(Idx)), Normal);
							if (Phi< Constraints[0].Phi)
							{
								Constraints[0].Phi = Phi;
								Constraints[0].Location = ATM.TransformPosition(SurfaceParticles.X(Idx));
								Constraints[0].Normal = BTM.TransformVector(Normal);
							}
						}
						return;
					}
					return;
				}
			}
		}

		ensureMsgf(false, TEXT("Unsupported convex to convex constraint."));

	}

	template class CollisionResolutionConvexConvex<float, 3>;

}
