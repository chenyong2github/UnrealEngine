// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolution.h"

#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Capsule.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Convex.h"
#include "Chaos/Defines.h"
#include "Chaos/HeightField.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"
#include "Chaos/GeometryQueries.h"

namespace Chaos
{
	namespace Collisions
	{
		template <typename T>
		struct TContactPoint
		{
			TVec3<T> Normal;
			TVec3<T> Location;
			T Phi;

			TContactPoint() : Phi(TNumericLimits<T>::Max()) {}
		};


		template <typename T>
		void UpdateContactPoint(TCollisionContact<T, 3>& Manifold, const TContactPoint<T>& NewContactPoint)
		{
			//for now just override
			if (NewContactPoint.Phi < Manifold.Phi)
			{
				Manifold.Normal = NewContactPoint.Normal;
				Manifold.Location = NewContactPoint.Location;
				Manifold.Phi = NewContactPoint.Phi;
			}
		}

		template <typename T, int d, typename GeometryA, typename GeometryB>
		TContactPoint<T> GJKContactPoint(const GeometryA& A, const TRigidTransform<T, d>& ATM, const GeometryB& B, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			TContactPoint<T> Contact;
			const TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);

			T Penetration;
			TVec3<T> ClosestA, ClosestB, Normal;
			if (GJKPenetration(A, B, BToATM, Penetration, ClosestA, ClosestB, Normal, (T)0))
			{
				Contact.Location = ATM.TransformPosition(ClosestA);
				Contact.Normal = -ATM.TransformVector(Normal);
				Contact.Phi = -Penetration;
			}
			else if (ensure(GJKDistance(A, B, BToATM, Penetration, ClosestA, ClosestB)))
			{
				//todo: make GJKPenetration support no penetration case
				TVector<T, d> NearPointAWorld = ATM.TransformPosition(ClosestA);
				TVector<T, d> NearPointBWorld = BTM.TransformPosition(ClosestB);
				TVector<T, d> NearPointBtoAWorld = NearPointAWorld - NearPointBWorld;
				Contact.Phi = Penetration;
				Contact.Normal = NearPointBtoAWorld.GetSafeNormal();
				Contact.Location = NearPointAWorld;
			}

			return Contact;
		}

		template <typename GeometryA, typename GeometryB, typename T, int d>
		TContactPoint<T> GJKImplicitContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const GeometryB& B, const TRigidTransform<T, d>& BTransform, const T Thickness)
		{
			TContactPoint<T> Contact;
			const TRigidTransform<T, d> AToBTM = ATransform.GetRelativeTransform(BTransform);

			T Penetration = FLT_MAX;
			TVec3<T> Location, Normal;
			if (const TImplicitObjectScaled<GeometryA>* ScaledConvexImplicit = A.template GetObject<const TImplicitObjectScaled<GeometryA> >())
			{
				if (B.GJKContactPoint(*ScaledConvexImplicit, AToBTM, Thickness, Location, Normal, Penetration))
				{
					Contact.Phi = Penetration;
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVector(Normal);
				}
			}
			else if (const GeometryA* ConvexImplicit = A.template GetObject<const GeometryA>())
			{
				if (B.GJKContactPoint(*ConvexImplicit, AToBTM, Thickness, Location, Normal, Penetration))
				{
					Contact.Phi = Penetration;
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVector(Normal);
				}
			}

			return Contact;
		}


		template<class T, int d>
		TContactPoint<T> ConvexConvexContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATM, const FImplicitObject& B, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			return CastHelper(A, [&](const auto& ADowncast)
			{
				return CastHelper(B, [&](const auto& BDowncast)
				{
					return GJKContactPoint(ADowncast, ATM, BDowncast, BTM, Thickness);
				});
			});
		}

		template <typename T, int d>
		void UpdateSingleShotManifold(TRigidBodyMultiPointContactConstraint<T, d>&  Constraint, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness)
		{
			// single shot manifolds for TConvex implicit object in the constraints implicit[0] position. 
			TContactPoint<T> ContactPoint = ConvexConvexContactPoint(*Constraint.Manifold.Implicit[0], Transform0, *Constraint.Manifold.Implicit[1], Transform1, Thickness);

			TArray<FVec3> CollisionSamples;
			//
			//  @todo(chaos) : Collision Manifold
			//   Remove the dependency on the virtual calls on the Implicit. Don't use FindClosestFaceAndVertices
			//   this relies on virtual calls on the ImplicitObject. Instead pass a parameters structures into 
			//   ConvexConvexContactPoint that can collect the face indices during evaluation of the support functions. 
			//   This can be implemented without virtual calls.
			//
			int32 FaceIndex = Constraint.Manifold.Implicit[0]->FindClosestFaceAndVertices(Transform0.InverseTransformPosition(ContactPoint.Location), CollisionSamples, 1.f);

			if (!ContactPoint.Normal.Equals(Constraint.PlaneNormal) || !Constraint.NumSamples())
			{
				Constraint.PlaneNormal = Transform1.InverseTransformVector(ContactPoint.Normal);
				Constraint.PlanePosition = Transform1.InverseTransformPosition(ContactPoint.Location - ContactPoint.Phi*ContactPoint.Normal);
			}


			if (FaceIndex != Constraint.SourceNormalIndex || !Constraint.NumSamples())
			{
				Constraint.ResetSamples(CollisionSamples.Num());
				Constraint.SourceNormalIndex = FaceIndex;

				//
				// @todo(chaos) : Collision Manifold
				//   Only save the four best samples and hard-code the size of Constraint.Samples to [len:4].
				//   Currently this just grabs all points and uses the deepest point for resolution. 
				//
				for (FVec3 Sample : CollisionSamples)
				{
					Constraint.AddSample({ Sample,0.f });
				}
			}
		}

		template <typename T, int d>
		void UpdateIterativeManifold(TRigidBodyMultiPointContactConstraint<T, d>&  Constraint, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness)
		{
			auto SumSampleData = [&](TRigidBodyMultiPointContactConstraint<T, d>& LambdaConstraint) -> TVector<float, 3>
			{
				TVector<float, 3> ReturnValue(0);
				for (int i = 0; i < LambdaConstraint.NumSamples(); i++)
				{
					ReturnValue += LambdaConstraint[i].X;
				}
				return ReturnValue;
			};

			// iterative manifolds for non TConvex implicit objects that require sampling 
			TContactPoint<T> ContactPoint = ConvexConvexContactPoint(*Constraint.Manifold.Implicit[0], Transform0, *Constraint.Manifold.Implicit[1], Transform1, Thickness);

			if (!ContactPoint.Normal.Equals(Constraint.PlaneNormal) || !Constraint.NumSamples())
			{
				Constraint.ResetSamples();
				Constraint.PlaneNormal = Transform1.InverseTransformVector(ContactPoint.Normal);
				Constraint.PlanePosition = Transform1.InverseTransformPosition(ContactPoint.Location - ContactPoint.Phi*ContactPoint.Normal);
			}

			TVector<T, d> SurfaceSample = Transform0.InverseTransformPosition(ContactPoint.Location);
			if (Constraint.NumSamples() < 4)
			{
				Constraint.AddSample({ SurfaceSample,0.f });
			}
			else if (Constraint.NumSamples() == 4)
			{
				TVector<T, d> Center = SumSampleData(Constraint) / Constraint.NumSamples();
				T Delta = (Center - SurfaceSample).SizeSquared();

				//
				// @todo(chaos) : Collision Manifold
				//    The iterative manifold need to be maximized for area instead of largest 
				//    distance from center.
				//
				T SmallestDelta = FLT_MAX;
				int32 SmallestIndex = 0;
				for (int32 idx = 0; idx < Constraint.NumSamples(); idx++)
					if (Constraint[idx].Delta < SmallestDelta) {
						SmallestDelta = Constraint[idx].Delta;
						SmallestIndex = idx;
					}

				if (Delta > SmallestDelta) {
					Constraint[SmallestIndex] = { SurfaceSample,Delta };
				}
			}
			else
			{
				ensure(false); // max of 4 points
			}

			typedef FRigidBodyMultiPointContactConstraint::FSampleData FSampleData;
			TVector<T, d> Center = SumSampleData(Constraint) / Constraint.NumSamples();
			for (int32 Index = 0; Index < Constraint.NumSamples(); Index++) { Constraint[Index].Delta = (Center - Constraint[Index].X).SizeSquared(); }
		}



		//
		// Box - Box
		//
		template <typename T, int d>
		TContactPoint<T> BoxBoxContactPoint(const TAABB<T, d>& Box1, const TRigidTransform<T, d>& ATM, const TAABB<T, d>& Box2, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			return GJKContactPoint(Box1, ATM, Box2, BTM, Thickness);
		}

		template <typename T, int d>
		void UpdateBoxBoxConstraint(const TAABB<T, d>& Box1, const TRigidTransform<T, d>& Box1Transform, const TAABB<T, d>& Box2, const TRigidTransform<T, d>& Box2Transform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, BoxBoxContactPoint(Box1, Box1Transform, Box2, Box2Transform, Thickness));
		}

		template <typename T, int d>
		void UpdateBoxBoxManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructBoxBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateBoxBoxConstraint(Object0->BoundingBox(), Transform0, Object1->BoundingBox(), Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}

		//
		// Box - HeightField
		//

		template <typename T, int d>
		TContactPoint<T> BoxHeightFieldContactPoint(const TAABB<T, d>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness)
		{
			return GJKImplicitContactPoint< TBox<float, 3> >(TBox<float, 3>(A), ATransform, B, BTransform, Thickness);
		}

		template <typename T, int d>
		void UpdateBoxHeightFieldConstraint(const TAABB<T, d>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, BoxHeightFieldContactPoint(A, ATransform, B, BTransform, Thickness));
		}

		template<class T, int d>
		void UpdateBoxHeightFieldManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructBoxHeightFieldConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateBoxHeightFieldConstraint(Object0->BoundingBox(), Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}



		//
		// Box-Plane
		//

		template <typename T, int d>
		bool UpdateBoxPlaneConstraint(const TAABB<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TCollisionContact<T, d> & Contact = Constraint.Manifold;

#if USING_CODE_ANALYSIS
			MSVC_PRAGMA(warning(push))
				MSVC_PRAGMA(warning(disable : ALL_CODE_ANALYSIS_WARNINGS))
#endif	// USING_CODE_ANALYSIS

				bool bApplied = false;
			const TRigidTransform<T, d> BoxToPlaneTransform(BoxTransform.GetRelativeTransform(PlaneTransform));
			const TVector<T, d> Extents = Box.Extents();
			constexpr int32 NumCorners = 2 + 2 * d;
			constexpr T Epsilon = KINDA_SMALL_NUMBER;

			TVector<T, d> Corners[NumCorners];
			int32 CornerIdx = 0;
			Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max());
			Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min());
			for (int32 j = 0; j < d; ++j)
			{
				Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min() + TVector<T, d>::AxisVector(j) * Extents);
				Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max() - TVector<T, d>::AxisVector(j) * Extents);
			}

#if USING_CODE_ANALYSIS
			MSVC_PRAGMA(warning(pop))
#endif	// USING_CODE_ANALYSIS

				TVector<T, d> PotentialConstraints[NumCorners];
			int32 NumConstraints = 0;
			for (int32 i = 0; i < NumCorners; ++i)
			{
				TVector<T, d> Normal;
				const T NewPhi = Plane.PhiWithNormal(Corners[i], Normal);
				if (NewPhi < Contact.Phi + Epsilon)
				{
					if (NewPhi <= Contact.Phi - Epsilon)
					{
						NumConstraints = 0;
					}
					Contact.Phi = NewPhi;
					Contact.Normal = PlaneTransform.TransformVector(Normal);
					Contact.Location = PlaneTransform.TransformPosition(Corners[i]);
					PotentialConstraints[NumConstraints++] = Contact.Location;
					bApplied = true;
				}
			}
			if (NumConstraints > 1)
			{
				TVector<T, d> AverageLocation(0);
				for (int32 ConstraintIdx = 0; ConstraintIdx < NumConstraints; ++ConstraintIdx)
				{
					AverageLocation += PotentialConstraints[ConstraintIdx];
				}
				Contact.Location = AverageLocation / NumConstraints;
			}

			return bApplied;
		}

		template<class T, int d>
		void UpdateBoxPlaneManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}


		template<typename T, int d>
		void ConstructBoxPlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const TPlane<T, d> * Object1 = Implicit1->template GetObject<const TPlane<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateBoxPlaneConstraint(Object0->BoundingBox(), Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}


		//
		// Sphere - Sphere
		//

		template <typename T, int d>
		TContactPoint<T> SphereSphereContactPoint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, const T Thickness)
		{
			TContactPoint<T> Result;

			const TVector<T, d> Center1 = Sphere1Transform.TransformPosition(Sphere1.GetCenter());
			const TVector<T, d> Center2 = Sphere2Transform.TransformPosition(Sphere2.GetCenter());
			const TVector<T, d> Direction = Center1 - Center2;
			const T Size = Direction.Size();
			const T NewPhi = Size - (Sphere1.GetRadius() + Sphere2.GetRadius());
			Result.Phi = NewPhi;
			Result.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
			Result.Location = Center1 - Sphere1.GetRadius() * Result.Normal;

			return Result;
		}

		template <typename T, int d>
		void UpdateSphereSphereConstraint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereSphereContactPoint(Sphere1, Sphere1Transform, Sphere2, Sphere2Transform, Thickness));
		}

		template<class T, int d>
		void UpdateSphereSphereManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSphereSphereConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TSphere<T, d> * Object1 = Implicit1->template GetObject<const TSphere<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateSphereSphereConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}

		//
		// Sphere - HeightField
		//

		template <typename T, int d>
		TContactPoint<T> SphereHeightFieldContactPoint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness)
		{
			return GJKImplicitContactPoint< TSphere<float, 3> >(TSphere<float, 3>(A), ATransform, B, BTransform, Thickness);
		}

		template <typename T, int d>
		void UpdateSphereHeightFieldConstraint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereHeightFieldContactPoint(A, ATransform, B, BTransform, Thickness));
		}

		template<class T, int d>
		void UpdateSphereHeightFieldManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSphereHeightFieldConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateSphereHeightFieldConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}

		//
		//  Sphere-Plane
		//

		template <typename T, int d>
		void UpdateSpherePlaneConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TCollisionContact<T, d> & Contact = Constraint.Manifold;

			const TRigidTransform<T, d> SphereToPlaneTransform(PlaneTransform.Inverse() * SphereTransform);
			const TVector<T, d> SphereCenter = SphereToPlaneTransform.TransformPosition(Sphere.GetCenter());

			TVector<T, d> NewNormal;
			T NewPhi = Plane.PhiWithNormal(SphereCenter, NewNormal);
			NewPhi -= Sphere.GetRadius();

			if (NewPhi < Contact.Phi)
			{
				Contact.Phi = NewPhi;
				Contact.Normal = PlaneTransform.TransformVectorNoScale(NewNormal);
				Contact.Location = SphereCenter - Contact.Normal * Sphere.GetRadius();
			}
		}

		template<class T, int d>
		void UpdateSpherePlaneManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSpherePlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TPlane<T, d> * Object1 = Implicit1->template GetObject<const TPlane<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateSpherePlaneConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}

		//
		// Sphere - Box
		//

		template <typename T, int d>
		TContactPoint<T> SphereBoxContactPoint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TAABB<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const T Thickness)
		{
			TContactPoint<T> Result;

			const TRigidTransform<T, d> SphereToBoxTransform(SphereTransform * BoxTransform.Inverse());	//todo: this should use GetRelative
			const TVector<T, d> SphereCenterInBox = SphereToBoxTransform.TransformPosition(Sphere.GetCenter());

			TVector<T, d> NewNormal;
			T NewPhi = Box.PhiWithNormal(SphereCenterInBox, NewNormal);
			NewPhi -= Sphere.GetRadius();

			Result.Phi = NewPhi;
			Result.Normal = BoxTransform.TransformVectorNoScale(NewNormal);
			Result.Location = SphereTransform.TransformPosition(Sphere.GetCenter()) - Result.Normal * Sphere.GetRadius();
			return Result;
		}

		template <typename T, int d>
		void UpdateSphereBoxConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TAABB<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereBoxContactPoint(Sphere, SphereTransform, Box, BoxTransform, Thickness));
		}

		template<class T, int d>
		void UpdateSphereBoxManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSphereBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateSphereBoxConstraint(*Object0, Transform0, Object1->BoundingBox(), Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}


		//
		// Sphere - Capsule
		//

		template <typename T, int d>
		TContactPoint<T> SphereCapsuleContactPoint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness)
		{
			TContactPoint<T> Result;

			FVector A1 = ATransform.TransformPosition(A.GetCenter());
			FVector B1 = BTransform.TransformPosition(B.GetX1());
			FVector B2 = BTransform.TransformPosition(B.GetX2());
			FVector P2 = FMath::ClosestPointOnSegment(A1, B1, B2);

			TVector<T, d> Delta = P2 - A1;
			T DeltaLen = Delta.Size();
			if (DeltaLen > KINDA_SMALL_NUMBER)
			{
				T NewPhi = DeltaLen - (A.GetRadius() + B.GetRadius());
				TVector<T, d> Dir = Delta / DeltaLen;
				Result.Phi = NewPhi;
				Result.Normal = -Dir;
				Result.Location = A1 + Dir * A.GetRadius();
			}

			return Result;
		}

		template <typename T, int d>
		void UpdateSphereCapsuleConstraint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereCapsuleContactPoint(A, ATransform, B, BTransform, Thickness));
		}

		template<class T, int d>
		void UpdateSphereCapsuleManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSphereCapsuleConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<T, d>* Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TCapsule<T>* Object1 = Implicit1->template GetObject<const TCapsule<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateSphereCapsuleConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}


		//
		// Capsule-Capsule
		//

		template <typename T, int d>
		TContactPoint<T> CapsuleCapsuleContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness)
		{
			TContactPoint<T> Result;

			FVector A1 = ATransform.TransformPosition(A.GetX1());
			FVector A2 = ATransform.TransformPosition(A.GetX2());
			FVector B1 = BTransform.TransformPosition(B.GetX1());
			FVector B2 = BTransform.TransformPosition(B.GetX2());
			FVector P1, P2;
			FMath::SegmentDistToSegmentSafe(A1, A2, B1, B2, P1, P2);

			TVector<T, d> Delta = P2 - P1;
			T DeltaLen = Delta.Size();
			if (DeltaLen > KINDA_SMALL_NUMBER)
			{
				T NewPhi = DeltaLen - (A.GetRadius() + B.GetRadius());
				TVector<T, d> Dir = Delta / DeltaLen;
				Result.Phi = NewPhi;
				Result.Normal = -Dir;
				Result.Location = P1 + Dir * A.GetRadius();
			}

			return Result;
		}

		template <typename T, int d>
		void UpdateCapsuleCapsuleConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleCapsuleContactPoint(A, ATransform, B, BTransform, Thickness));
		}

		template<class T, int d>
		void UpdateCapsuleCapsuleManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const TCapsule<T> * Object1 = Implicit1->template GetObject<const TCapsule<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateCapsuleCapsuleConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}

		//
		// Capsule - Box
		//

		template <typename T, int d>
		TContactPoint<T> CapsuleBoxContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TAABB<T, d>& B, const TRigidTransform<T, d>& BTransform, const T Thickness)
		{
			return GJKContactPoint(A, ATransform, B, BTransform, Thickness);
		}

		template <typename T, int d>
		void UpdateCapsuleBoxConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TAABB<T, d>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleBoxContactPoint(A, ATransform, B, BTransform, Thickness));
		}

		template<class T, int d>
		void UpdateCapsuleBoxManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructCapsuleBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateCapsuleBoxConstraint(*Object0, Transform0, Object1->BoundingBox(), Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}

		//
		// Capsule-HeightField
		//

		template <typename T, int d>
		TContactPoint<T> CapsuleHeightFieldContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness)
		{
			return GJKImplicitContactPoint< TCapsule<float> >(TCapsule<float>(A), ATransform, B, BTransform, Thickness);
		}

		template <typename T, int d>
		void UpdateCapsuleHeightFieldConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleHeightFieldContactPoint(A, ATransform, B, BTransform, Thickness));
		}

		template<class T, int d>
		void UpdateCapsuleHeightFieldManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructCapsuleHeightFieldConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateCapsuleHeightFieldConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}

		//
		// Convex - Convex
		//

		template<class T, int d>
		void UpdateConvexConvexConstraint(const FImplicitObject& Implicit0, const TRigidTransform<T, d>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<T, d>& Transform1, const T Thickness, TCollisionConstraintBase<T, d>& ConstraintBase)
		{
			TContactPoint<T> ContactPoint;

			if (ConstraintBase.GetType() == FRigidBodyPointContactConstraint::StaticType())
			{
				ContactPoint = ConvexConvexContactPoint(Implicit0, Transform0, Implicit1, Transform1, Thickness);
			}
			else if (ConstraintBase.GetType() == FRigidBodyMultiPointContactConstraint::StaticType())
			{
				TRigidBodyMultiPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyMultiPointContactConstraint<T, d>>();
				ContactPoint.Phi = FLT_MAX;

				const TRigidTransform<T, d> AToBTM = Transform0.GetRelativeTransform(Transform1);

				TPlane<T, d> CollisionPlane(Constraint.PlanePosition, Constraint.PlaneNormal);

				// re-sample the constraint based on the distance from the collision plane.
				for (int32 Idx = 0; Idx < Constraint.NumSamples(); Idx++)
				{
					Constraint[Idx].Manifold.Phi = CollisionPlane.PhiWithNormal(AToBTM.TransformPosition(Constraint[Idx].X), Constraint[Idx].Manifold.Normal);
					Constraint[Idx].Manifold.Normal = Transform1.TransformVector(Constraint.PlaneNormal);
					Constraint[Idx].Manifold.Location = Transform0.TransformPosition(Constraint[Idx].X);

					// save the best point for collision processing	
					if (ContactPoint.Phi > Constraint[Idx].Manifold.Phi)
					{
						ContactPoint.Phi = Constraint[Idx].Manifold.Phi;
						ContactPoint.Normal = Constraint[Idx].Manifold.Normal;
						ContactPoint.Location = Constraint[Idx].Manifold.Location;
					}
				}
			}

			UpdateContactPoint(ConstraintBase.Manifold, ContactPoint);
		}

		template<class T, int d>
		void UpdateConvexConvexManifold(TCollisionConstraintBase<T, d>&  ConstraintBase, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness)
		{
			if (TRigidBodyMultiPointContactConstraint<T,d>* Constraint = ConstraintBase.template As<TRigidBodyMultiPointContactConstraint<T, d>>())
			{
				if (GetInnerType(ConstraintBase.Manifold.Implicit[0]->GetType()) == ImplicitObjectType::Convex)
				{
					UpdateSingleShotManifold(*Constraint, Transform0, Transform1, Thickness);
				}
				else
				{
					UpdateIterativeManifold(*Constraint, Transform0, Transform1, Thickness);
				}
			}
		}


		template<class T, int d>
		void ConstructConvexConvexConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
			TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
			FRigidBodyMultiPointContactConstraint* Constraint = new FRigidBodyMultiPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

			UpdateConvexConvexManifold(*Constraint, Transform0, Transform1, Thickness);
			UpdateConvexConvexConstraint(*Implicit0, Transform0, *Implicit1, Transform1, Thickness, *Constraint);

			if (Constraint->GetPhi() < Thickness)
			{
				NewConstraints.Add(Constraint);
			}
			else
			{
				delete Constraint;
			}

		}

		//
		// Convex - HeightField
		//

		template <typename T, int d>
		TContactPoint<T> ConvexHeightFieldContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness)
		{
			return GJKImplicitContactPoint< FConvex >(A, ATransform, B, BTransform, Thickness);
		}

		template <typename T, int d>
		void UpdateConvexHeightFieldConstraint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, ConvexHeightFieldContactPoint(A, ATransform, B, BTransform, Thickness));
		}

		template<class T, int d>
		void UpdateConvexHeightFieldManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructConvexHeightFieldConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{

			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Implicit0->IsConvex() && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

				UpdateConvexHeightFieldConstraint(*Implicit0, Transform0, *Object1, Transform1, Thickness, *Constraint);

				if (Constraint->GetPhi() < Thickness)
				{
					NewConstraints.Add(Constraint);
				}
				else
				{
					delete Constraint;
				}
			}
		}



		//
		// Levelset-Levelset
		//

		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetLevelsetConstraint"), STAT_UpdateLevelsetLevelsetConstraint, STATGROUP_ChaosWide);
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateLevelsetLevelsetConstraint(const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetLevelsetConstraint);

			TGenericParticleHandle<T, d> Particle0 = Constraint.Particle[0];
			TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
			if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
			{
				return;
			}

			TGenericParticleHandle<T, d> Particle1 = Constraint.Particle[1];
			TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());
			if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
			{
				return;
			}

			const TBVHParticles<T, d>* SampleParticles = nullptr;
			SampleParticles = Particle0->CollisionParticles().Get();

			if (SampleParticles)
			{
				SampleObject<UpdateType>(*Particle1->Geometry(), LevelsetTM, *SampleParticles, ParticlesTM, Thickness, Constraint);
			}
		}

		template<class T, int d>
		void UpdateLevelsetLevelsetManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}


		template<typename T, int d>
		void ConstructLevelsetLevelsetConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
			TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
			FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint(nullptr, nullptr, ParticleImplicit0TM, nullptr, nullptr, ParticleImplicit1TM);

			bool bIsParticleDynamic0 = Particle0->CastToRigidParticle() && Particle0->ObjectState() == EObjectStateType::Dynamic;
			if (!Particle1->Geometry() || (bIsParticleDynamic0 && !Particle0->CastToRigidParticle()->CollisionParticlesSize() && Particle0->Geometry() && !Particle0->Geometry()->IsUnderlyingUnion()))
			{
				Constraint->Particle[0] = Particle1;
				Constraint->Particle[1] = Particle0;
				Constraint->SetManifold(Implicit1, Implicit0);
			}
			else
			{
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->SetManifold(Implicit0, Implicit1);
			}

			UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Any>(Thickness, *Constraint);

			if (Constraint->GetPhi() < Thickness)
			{
				NewConstraints.Add(Constraint);
			}
			else
			{
				delete Constraint;
			}
		}

		//
		//  Union-Union
		//

		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::ConstructUnionUnionConstraints"), STAT_ConstructUnionUnionConstraints, STATGROUP_ChaosWide);
		template<typename T, int d>
		void ConstructUnionUnionConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			SCOPE_CYCLE_COUNTER(STAT_ConstructUnionUnionConstraints);

			const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(Implicit0, Transform0, *Implicit1, Transform1, Thickness);

			for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
			{
				const FImplicitObject* LevelsetInnerObj = LevelsetObjPair.First;
				const TRigidTransform<T, d>& LevelsetInnerObjTM = LevelsetObjPair.Second * Transform1;

				//now find all particle inner objects
				const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes(LevelsetInnerObj, LevelsetInnerObjTM, *Implicit0, Transform0, Thickness);

				//for each inner obj pair, update constraint
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticlePair : ParticleShapes)
				{
					const FImplicitObject* ParticleInnerObj = ParticlePair.First;
					ConstructConstraints<T, d>(Particle0, Particle1, ParticleInnerObj, LevelsetInnerObj, Transform0, Transform1, Thickness, NewConstraints);
				}
			}
		}

		//
		// Constraint API
		//

		template<typename T, int d>
		void UpdateManifold(TCollisionConstraintBase<T, d>& ConstraintBase, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T Thickness)
		{
			const FImplicitObject& Implicit0 = *ConstraintBase.Manifold.Implicit[0];
			const FImplicitObject& Implicit1 = *ConstraintBase.Manifold.Implicit[1];

			const TRigidTransform<T, d>& Transform0 = ConstraintBase.ImplicitTransform[0] * ATM;
			const TRigidTransform<T, d>& Transform1 = ConstraintBase.ImplicitTransform[1] * BTM;

#if !UE_BUILD_SHIPPING
			EImplicitObjectType Implicit0OuterType = Implicit0.GetType();
			EImplicitObjectType Implicit1OuterType = Implicit1.GetType();

			if (Implicit0OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
			else if (Implicit0OuterType != FImplicitObjectUnion::StaticType() && Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
			else if (Implicit0OuterType == FImplicitObjectUnion::StaticType() && Implicit1OuterType != FImplicitObjectUnion::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
			else if (Implicit0OuterType == FImplicitObjectUnion::StaticType() && Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
#endif
			//
			// @todo(chaos): Collision Constraints (CollisionMap)
			//    Modify Construct() and Update() use a CollisionMap indexed on 
			//    EImplicitObjectType, instead of the if/else chain. Also, remove 
			//    the blocks with the ensure(false), they are just for validation 
			//    after the recent change. 
			//

			EImplicitObjectType Implicit0Type = GetInnerType(Implicit0.GetType());
			EImplicitObjectType Implicit1Type = GetInnerType(Implicit1.GetType()); 
			
			if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateBoxBoxManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateBoxHeightFieldManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				UpdateSphereSphereManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateSphereHeightFieldManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateBoxPlaneManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateSpherePlaneManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateSphereBoxManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateSphereCapsuleManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateCapsuleCapsuleManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateCapsuleBoxManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateCapsuleHeightFieldManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ensure(false);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				//UpdatePlaneBoxManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ensure(false);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//UpdatePlaneSphereManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//UpdateBoxSphereManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				//UpdateBoxSphereManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//UpdateCapsuleSphereManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1.IsConvex())
			{
				ensure(false);
			}
			else if (Implicit0.IsConvex() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateConvexHeightFieldManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else if (Implicit0.IsConvex() && Implicit1.IsConvex())
			{
				UpdateConvexConvexManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
			else
			{
				UpdateLevelsetLevelsetManifold(ConstraintBase, Transform0, Transform1, Thickness);
			}
		}

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateConstraint(TCollisionConstraintBase<T, d>& ConstraintBase, const TRigidTransform<T, d>& ParticleTransform0, const TRigidTransform<T, d>& ParticleTransform1, const T Thickness)
		{
			const FImplicitObject& Implicit0 = *ConstraintBase.Manifold.Implicit[0];
			const FImplicitObject& Implicit1 = *ConstraintBase.Manifold.Implicit[1];

			const TRigidTransform<T, d>& Transform0 = ConstraintBase.ImplicitTransform[0] * ParticleTransform0;
			const TRigidTransform<T, d>& Transform1 = ConstraintBase.ImplicitTransform[1] * ParticleTransform1;

#if !UE_BUILD_SHIPPING
			EImplicitObjectType Implicit0OuterType = Implicit0.GetType();
			EImplicitObjectType Implicit1OuterType = Implicit1.GetType();

			if (Implicit0OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
			else if (Implicit0OuterType != FImplicitObjectUnion::StaticType() && Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
			else if (Implicit0OuterType == FImplicitObjectUnion::StaticType() && Implicit1OuterType != FImplicitObjectUnion::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
			else if (Implicit0OuterType == FImplicitObjectUnion::StaticType() && Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				ensure(false);//should not be possible to get this type, it should already be resolved by the constraint. (see ConstructConstraints)
				return;
			}
#endif

			//
			// @todo(chaos): Collision Constraints (CollisionMap)
			//    Modify Construct() and Update() use a CollisionMap indexed on 
			//    EImplicitObjectType, instead of the if/else chain. Also, remove 
			//    the blocks with the ensure(false), they are just for validation 
			//    after the recent change. 
			//
			EImplicitObjectType Implicit0Type = GetInnerType(Implicit0.GetType());
			EImplicitObjectType Implicit1Type = GetInnerType(Implicit1.GetType()); 
			
			if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateBoxBoxConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateBoxHeightFieldConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				UpdateSphereSphereConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TSphere<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateSphereHeightFieldConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateBoxPlaneConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *Implicit1.template GetObject<TPlane<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateSpherePlaneConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TPlane<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateSphereBoxConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateSphereCapsuleConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateCapsuleCapsuleConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateCapsuleBoxConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateCapsuleHeightFieldConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				//     This case should not be necessary. The height fields 
				//     will only ever be collided against, so ideally will never 
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always 
				//     second.
				ensure(false);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				TRigidBodyPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >();
				TRigidBodyPointContactConstraint<T, d> TmpConstraint = Constraint;
				UpdateBoxPlaneConstraint(Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, *Implicit0.template GetObject<TPlane<T, d>>(), Transform0, Thickness, TmpConstraint);
				if (TmpConstraint.GetPhi() < Constraint.GetPhi())
				{
					Constraint = TmpConstraint;
					Constraint.SetNormal(-Constraint.GetNormal());
				}
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//     This case should not be necessary. The height fields 
				//     will only ever be collided against, so ideally will never 
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always 
				//     second.
				ensure(false);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				TRigidBodyPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >();
				TRigidBodyPointContactConstraint<T, d> TmpConstraint = Constraint;
				UpdateSpherePlaneConstraint(*Implicit1.template GetObject<TSphere<T, d>>(), Transform1, *Implicit0.template GetObject<TPlane<T, d>>(), Transform0, Thickness, TmpConstraint);
				if (TmpConstraint.GetPhi() < Constraint.GetPhi())
				{
					Constraint = TmpConstraint;
					Constraint.SetNormal(-Constraint.GetNormal());
				}
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				TRigidBodyPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >();
				TRigidBodyPointContactConstraint<T, d> TmpConstraint = Constraint;
				UpdateSphereBoxConstraint(*Implicit1.template GetObject<TSphere<T, d>>(), Transform1, Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, Thickness, TmpConstraint);
				if (TmpConstraint.GetPhi() < Constraint.GetPhi())
				{
					Constraint = TmpConstraint;
					Constraint.SetNormal(-Constraint.GetNormal());
				}
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				TRigidBodyPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >();
				TRigidBodyPointContactConstraint<T, d> TmpConstraint = Constraint;
				UpdateCapsuleBoxConstraint(*Implicit1.template GetObject<TCapsule<T>>(), Transform1, Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, Thickness, TmpConstraint);
				if (TmpConstraint.GetPhi() < Constraint.GetPhi())
				{
					Constraint = TmpConstraint;
					Constraint.SetNormal(-Constraint.GetNormal());
				}
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				TRigidBodyPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >();
				TRigidBodyPointContactConstraint<T, d> TmpConstraint = Constraint;
				UpdateSphereCapsuleConstraint(*Implicit1.template GetObject<TSphere<T, d>>(), Transform1, *Implicit0.template GetObject<TCapsule<T>>(), Transform0, Thickness, TmpConstraint);
				if (TmpConstraint.GetPhi() < Constraint.GetPhi())
				{
					Constraint = TmpConstraint;
					Constraint.SetNormal(-Constraint.GetNormal());
				}
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//     This case should not be necessary. The height fields 
				//     will only ever be collided against, so ideally will never 
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always 
				//     second.
				ensure(false);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1.IsConvex())
			{
				//     This case should not be necessary. The height fields 
				//     will only ever be collided against, so ideally will never 
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always 
				//     second.
				ensure(false);
			}
			else if (Implicit0.IsConvex() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateConvexHeightFieldConstraint(Implicit0, Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0.IsConvex() && Implicit1.IsConvex())
			{
				UpdateConvexConvexConstraint(Implicit0, Transform0, Implicit1, Transform1, Thickness, ConstraintBase);
			}
			else
			{
				UpdateLevelsetLevelsetConstraint<UpdateType>(Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >());
			}
		}

		template<typename T, int d>
		void ConstructConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			//  TriMesh Culling
			if (Implicit0 && Implicit0->GetType() == ImplicitObjectType::TriangleMesh) return;
			if (Implicit1 && Implicit1->GetType() == ImplicitObjectType::TriangleMesh) return;

			// If either shape is disabled for collision bail without constructing a constraint
			const TPerShapeData<T, d>* Shape0 = Particle0->GetImplicitShape(Implicit0);
			const TPerShapeData<T, d>* Shape1 = Particle1->GetImplicitShape(Implicit1);
			if ((Shape0 && Shape0->bDisable) || (Shape1 && Shape1->bDisable))
			{
				return;
			}

			if (!Implicit0 || !Implicit1)
			{
				ConstructLevelsetLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
				return;
			}

			//
			// @todo(chaos): Collision Constraints (CollisionMap)
			//    Modify Construct() and Update() use a CollisionMap indexed on EImplicitObjectType, instead of the if/else chain
			//
			EImplicitObjectType Implicit0OuterType = Implicit0->GetType();
			EImplicitObjectType Implicit1OuterType = Implicit1->GetType();

			if (Implicit0OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform0 = TransformedImplicit0->GetTransform() * Transform0;
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), Implicit1, TransformedTransform0, Transform1, Thickness, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform1 = TransformedImplicit1->GetTransform() * Transform1;
				ConstructConstraints(Particle0, Particle1, Implicit0, TransformedImplicit1->GetTransformedObject(), Transform0, TransformedTransform1, Thickness, NewConstraints);
				return;
			}
			else if (Implicit0OuterType != FImplicitObjectUnion::StaticType() && Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(Implicit0, Transform0, *Implicit1, Transform1, Thickness);
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
				{
					const FImplicitObject* Implicit1InnerObj = LevelsetObjPair.First;
					const TRigidTransform<T, d> Implicit1InnerObjTM = LevelsetObjPair.Second * Transform1;
					ConstructConstraints(Particle0, Particle1, Implicit0, Implicit1InnerObj, Transform0, Implicit1InnerObjTM, Thickness, NewConstraints);
				}
				return;
			}
			else if (Implicit0OuterType == FImplicitObjectUnion::StaticType() && Implicit1OuterType != FImplicitObjectUnion::StaticType())
			{
				// [Note] forces non-unions into particle[0] position
				const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(Implicit1, Transform1, *Implicit0, Transform0, Thickness);
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
				{
					const FImplicitObject* Implicit0InnerObj = LevelsetObjPair.First;
					const TRigidTransform<T, d> Implicit0InnerObjTM = LevelsetObjPair.Second * Transform0;
					ConstructConstraints(Particle0, Particle1, Implicit0InnerObj, Implicit1, Implicit0InnerObjTM, Transform1, Thickness, NewConstraints);
				}
				return;
			}
			else if (Implicit0OuterType == FImplicitObjectUnion::StaticType() && Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				ConstructUnionUnionConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
				return;
			}

			EImplicitObjectType Implicit0Type = GetInnerType(Implicit0->GetType());
			EImplicitObjectType Implicit1Type = GetInnerType(Implicit1->GetType());

			if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				ConstructBoxHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				ConstructBoxPlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxPlaneConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereSphereConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				ConstructSphereHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				ConstructSpherePlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSpherePlaneConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructSphereBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereBoxConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructSphereCapsuleConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereCapsuleConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructCapsuleCapsuleConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructCapsuleBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructCapsuleBoxConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				ConstructCapsuleHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructCapsuleHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0->IsConvex() && Implicit1Type == THeightField<T>::StaticType())
			{
				ConstructConvexHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1->IsConvex())
			{
				ConstructConvexHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0->IsConvex() && Implicit1->IsConvex())
			{
				ConstructConvexConvexConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else
			{
				ConstructLevelsetLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
		}

		template void UpdateBoxBoxConstraint<float, 3>(const TAABB<float, 3>& Box1, const TRigidTransform<float, 3>& Box1Transform, const TAABB<float, 3>& Box2, const TRigidTransform<float, 3>& Box2Transform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateBoxBoxManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructBoxBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateBoxHeightFieldConstraint<float, 3>(const TAABB<float, 3>& A, const TRigidTransform<float, 3>& ATransform, const THeightField<float>& B, const TRigidTransform<float, 3>& BTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateBoxHeightFieldManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructBoxHeightFieldConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template bool UpdateBoxPlaneConstraint<float, 3>(const TAABB<float, 3>& Box, const TRigidTransform<float, 3>& BoxTransform, const TPlane<float, 3>& Plane, const TRigidTransform<float, 3>& PlaneTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateBoxPlaneManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructBoxPlaneConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereSphereConstraint<float, 3>(const TSphere<float, 3>& Sphere1, const TRigidTransform<float, 3>& Sphere1Transform, const TSphere<float, 3>& Sphere2, const TRigidTransform<float, 3>& Sphere2Transform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSphereSphereManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructSphereSphereConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereHeightFieldConstraint<float, 3>(const TSphere<float, 3>& A, const TRigidTransform<float, 3>& ATransform, const THeightField<float>& B, const TRigidTransform<float, 3>& BTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSphereHeightFieldManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructSphereHeightFieldConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSpherePlaneConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TPlane<float, 3>& Plane, const TRigidTransform<float, 3>& PlaneTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSpherePlaneManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructSpherePlaneConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereBoxConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TAABB<float, 3>& Box, const TRigidTransform<float, 3>& BoxTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSphereBoxManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructSphereBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereCapsuleConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TCapsule<float>& Box, const TRigidTransform<float, 3>& BoxTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSphereCapsuleManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructSphereCapsuleConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateCapsuleCapsuleConstraint<float, 3>(const TCapsule<float>& A, const TRigidTransform<float, 3>& ATransform, const TCapsule<float>& B, const TRigidTransform<float, 3>& BTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateCapsuleCapsuleManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructCapsuleCapsuleConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateCapsuleBoxConstraint<float, 3>(const TCapsule<float>& A, const TRigidTransform<float, 3>& ATransform, const TAABB<float, 3>& B, const TRigidTransform<float, 3>& BTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateCapsuleBoxManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructCapsuleBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateCapsuleHeightFieldConstraint<float, 3>(const TCapsule<float>& A, const TRigidTransform<float, 3>& ATransform, const THeightField<float>& B, const TRigidTransform<float, 3>& BTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateCapsuleHeightFieldManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructCapsuleHeightFieldConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateConvexConvexConstraint<float, 3>(const FImplicitObject& A, const TRigidTransform<float, 3>& ATM, const FImplicitObject& B, const TRigidTransform<float, 3>& BTM, const float Thickness, TCollisionConstraintBase<float, 3>& Constraint);
		template void UpdateConvexConvexManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructConvexConvexConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateConvexHeightFieldConstraint<float, 3>(const FImplicitObject& A, const TRigidTransform<float, 3>& ATransform, const THeightField<float>& B, const TRigidTransform<float, 3>& BTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateConvexHeightFieldManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructConvexHeightFieldConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Any, float, 3>(const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest, float, 3>(const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateLevelsetLevelsetManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void ConstructLevelsetLevelsetConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void ConstructUnionUnionConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSingleShotManifold<float, 3>(TRigidBodyMultiPointContactConstraint<float, 3>&  Constraint, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness);
		template void UpdateIterativeManifold<float, 3>(TRigidBodyMultiPointContactConstraint<float, 3>&  Constraint, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness);
		template void UpdateManifold<float, 3>(TCollisionConstraintBase<float, 3>& ConstraintBase, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float Thickness);
		template void UpdateConstraint<ECollisionUpdateType::Any, float, 3>(TCollisionConstraintBase<float, 3>& ConstraintBase, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness);
		template void UpdateConstraint<ECollisionUpdateType::Deepest, float, 3>(TCollisionConstraintBase<float, 3>& ConstraintBase, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness);
		template void ConstructConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

	} // Collisions

} // Chaos
