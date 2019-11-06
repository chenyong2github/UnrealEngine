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

		for (int i = 0; i < ManifoldSamples; i++)
		{
			TRigidBodyContactConstraint<T, d> Constraint;
			Constraint.Particle = Particle0;
			Constraint.Levelset = Particle1;
			Constraint.Geometry[0] = Implicit0;
			Constraint.Geometry[1] = Implicit1;
			ConstraintBuffer.Add(Constraint);
		}
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

		unsigned char AType = A.GetType();
		unsigned char AStaticType = ImplicitObjectType::IsScaled;

		if (ensure(IsScaled(A.GetType()) && IsScaled(B.GetType())))
		{
			const TImplicitObjectScaled<TConvex<T, d>>& AScaled = static_cast<const TImplicitObjectScaled<TConvex<T, d>>&>(A);
			const TImplicitObjectScaled<TConvex<T, d>>& BScaled = static_cast<const TImplicitObjectScaled<TConvex<T, d>>&>(B);

			TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);
			TRigidTransform<T, d> AToBTM = ATM.GetRelativeTransform(BTM);

			if (ensure(GetInnerType(A.GetType()) == ImplicitObjectType::Convex && GetInnerType(B.GetType()) == ImplicitObjectType::Convex))
			{
				const TConvex<T, d>* AObject = static_cast<const TConvex<T, d>*>(AScaled.Object().Get());
				const TConvex<T, d>* BObject = static_cast<const TConvex<T, d>*>(BScaled.Object().Get());

				const TParticles<T, d>& SurfaceParticles = AObject->GetSurfaceParticles();

				T OutDistance;
				TVector<T, d> StubNormal;
				TVector<T, d> OutNearestA;
				TVector<T, d> OutNearestB;
				TVector<T, d> OutNormal;

				if (Manifold == nullptr)
				{
					if (GJKDistance<T>(*AObject, *BObject, BToATM, OutDistance, OutNearestA, OutNearestB))
					{
						TVector<T, d> Normal(0);
						TVector<T, d> WorldA = ATM.TransformPosition(OutNearestA);
						TVector<T, d> WorldB = BTM.TransformPosition(OutNearestB);
						TVector<T, d> CenterLocation = WorldA;
						T Phi = BObject->PhiWithNormal(BTM.InverseTransformPosition(CenterLocation), Normal);

						T ManifoldSize = A.BoundingBox().Extents().Size() * ManifoldScale;
						TVector<T, d> CrossVector = FMath::IsNearlyEqual(FMath::Abs(TVector<T, d>::DotProduct(TVector<T, d>(0, 1, 0), Normal)), T(1)) ?
							TVector<T, d>(0, 0, 1) :
							TVector<T, d>(0, 1, 0);

						Constraints[0].LocalLocation = ATM.InverseTransformPosition(CenterLocation);
						Constraints[0].Normal = Normal;

						if (NumConstraints > 1)
						{
							TVector<T, d> DirVector = CrossVector;
							T Angle = T(360) / T(NumConstraints - 1), LocalPhi;
							for (int32 Idx = 1; Idx < NumConstraints; Idx++)
							{
								CrossVector = CrossVector.RotateAngleAxis(Angle, Normal);
								TVector<T, d> WorldLocation = CenterLocation + ManifoldSize * CrossVector;
								Constraints[Idx].LocalLocation = ATM.InverseTransformPosition(WorldLocation);

								LocalPhi = AObject->PhiWithNormal(Constraints[Idx].LocalLocation, StubNormal);
								Constraints[Idx].LocalLocation -= StubNormal * LocalPhi;

								Constraints[Idx].Normal = Normal;
							}
						}
					}
				}

				for (int32 Idx = 0; Idx < NumConstraints; Idx++)
				{
					Constraints[Idx].Location = ATM.TransformPosition(Constraints[Idx].LocalLocation);
					Constraints[Idx].Phi = BObject->PhiWithNormal(BTM.InverseTransformPosition(Constraints[Idx].Location), StubNormal);

					if (Constraints[Idx].Phi < 0.f && Manifold)
					{
						Manifold->SetTimestamp(INT_MAX);
					}
				}

				return;
			}
		}

		ensureMsgf(false, TEXT("Unsupported convex to convex constraint."));

	}

	template class CollisionResolutionConvexConvex<float, 3>;

}
