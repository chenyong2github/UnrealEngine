// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolution.h"

#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Capsule.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Convex.h"
#include "Chaos/Defines.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"


namespace Chaos
{
	namespace Collisions
	{
		//
		// Box - Box
		//

		template <typename T, int d>
		void UpdateBoxBoxConstraint(const TBox<T, d>& Box1, const TRigidTransform<T, d>& Box1Transform, const TBox<T, d>& Box2, const TRigidTransform<T, d>& Box2Transform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TPointContactManifold<T, d> & Contact = Constraint.Manifold;

			TBox<T, d> Box2SpaceBox1 = Box1.TransformedBox(Box1Transform * Box2Transform.Inverse());
			TBox<T, d> Box1SpaceBox2 = Box2.TransformedBox(Box2Transform * Box1Transform.Inverse());
			Box2SpaceBox1.Thicken(Thickness);
			Box1SpaceBox2.Thicken(Thickness);
			if (Box1SpaceBox2.Intersects(Box1) && Box2SpaceBox1.Intersects(Box2))
			{
				const TVector<T, d> Box1Center = (Box1Transform * Box2Transform.Inverse()).TransformPosition(Box1.Center());
				bool bDeepOverlap = false;
				if (Box2.SignedDistance(Box1Center) < 0)
				{
					//If Box1 is overlapping Box2 by this much the signed distance approach will fail (box1 gets sucked into box2). In this case just use two spheres
					TSphere<T, d> Sphere1(Box1Transform.TransformPosition(Box1.Center()), Box1.Extents().Min() / 2);
					TSphere<T, d> Sphere2(Box2Transform.TransformPosition(Box2.Center()), Box2.Extents().Min() / 2);
					const TVector<T, d> Direction = Sphere1.GetCenter() - Sphere2.GetCenter();
					T Size = Direction.Size();
					if (Size < (Sphere1.GetRadius() + Sphere2.GetRadius()))
					{
						const T NewPhi = Size - (Sphere1.GetRadius() + Sphere2.GetRadius());;
						if (NewPhi < Contact.Phi)
						{
							bDeepOverlap = true;
							Contact.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
							Contact.Phi = NewPhi;
							Contact.Location = Sphere1.GetCenter() - Sphere1.GetRadius() * Contact.Normal;
						}
					}
				}
				if (!bDeepOverlap || Contact.Phi >= 0)
				{
					//if we didn't have deep penetration use signed distance per particle. If we did have deep penetration but the spheres did not overlap use signed distance per particle

					//UpdateLevelsetConstraintGJK(InParticles, Thickness, Constraint);
					//check(Contact.Phi < MThickness);
					// For now revert to doing all points vs lsv check until we can figure out a good way to get the deepest point without needing this
					{
						const TArray<TVector<T, d>> SampleParticles = Box1.ComputeSamplePoints();
						const TRigidTransform<T, d> Box1ToBox2Transform = Box1Transform.GetRelativeTransform(Box2Transform);
						int32 NumParticles = SampleParticles.Num();
						for (int32 i = 0; i < NumParticles; ++i)
						{
							SampleObjectHelper(Box2, Box2Transform, Box1ToBox2Transform, SampleParticles[i], Thickness, Constraint);
						}
					}
				}
			}
		}


		template<typename T, int d>
		void ConstructBoxBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

				UpdateBoxBoxConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

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
		bool UpdateBoxPlaneConstraint(const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TPointContactManifold<T, d> & Contact = Constraint.Manifold;

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


		template<typename T, int d>
		void ConstructBoxPlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			
			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const TPlane<T, d> * Object1 = Implicit1->template GetObject<const TPlane<T, d> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

				UpdateBoxPlaneConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

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
		void UpdateSphereSphereConstraint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TPointContactManifold<T, d> & Contact = Constraint.Manifold;

			const TVector<T, d> Center1 = Sphere1Transform.TransformPosition(Sphere1.GetCenter());
			const TVector<T, d> Center2 = Sphere2Transform.TransformPosition(Sphere2.GetCenter());
			const TVector<T, d> Direction = Center1 - Center2;
			const T Size = Direction.Size();
			const T NewPhi = Size - (Sphere1.GetRadius() + Sphere2.GetRadius());
			if (NewPhi < Contact.Phi)
			{
				Contact.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
				Contact.Phi = NewPhi;
				Contact.Location = Center1 - Sphere1.GetRadius() * Contact.Normal;
			}
		}

		template<typename T, int d>
		void ConstructSphereSphereConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			
			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TSphere<T, d> * Object1 = Implicit1->template GetObject<const TSphere<T, d> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

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
		//  Sphere-Plane
		//

		template <typename T, int d>
		void UpdateSpherePlaneConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TPointContactManifold<T, d> & Contact = Constraint.Manifold;

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

		template<typename T, int d>
		void ConstructSpherePlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			
			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TPlane<T, d> * Object1 = Implicit1->template GetObject<const TPlane<T, d> >();
			if (ensure(Object0 && Object1))
			{

				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

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
		void UpdateSphereBoxConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TPointContactManifold<T, d> & Contact = Constraint.Manifold;

			const TRigidTransform<T, d> SphereToBoxTransform(SphereTransform * BoxTransform.Inverse());
			const TVector<T, d> SphereCenterInBox = SphereToBoxTransform.TransformPosition(Sphere.GetCenter());

			TVector<T, d> NewNormal;
			T NewPhi = Box.PhiWithNormal(SphereCenterInBox, NewNormal);
			NewPhi -= Sphere.GetRadius();

			if (NewPhi < Contact.Phi)
			{
				Contact.Phi = NewPhi;
				Contact.Normal = BoxTransform.TransformVectorNoScale(NewNormal);
				Contact.Location = SphereTransform.TransformPosition(Sphere.GetCenter()) - Contact.Normal * Sphere.GetRadius();
			}
		}

		template<typename T, int d>
		void ConstructSphereBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			
			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

				UpdateSphereBoxConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

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
		void UpdateSphereCapsuleConstraint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TPointContactManifold<T, d>& Contact = Constraint.Manifold;

			FVector A1 = ATransform.TransformPosition(A.GetCenter());
			FVector B1 = BTransform.TransformPosition(B.GetX1());
			FVector B2 = BTransform.TransformPosition(B.GetX2());
			FVector P2 = FMath::ClosestPointOnSegment(A1, B1, B2);

			TVector<T, d> Delta = P2 - A1;
			T DeltaLen = Delta.Size();
			if (DeltaLen > KINDA_SMALL_NUMBER)
			{
				T NewPhi = DeltaLen - (A.GetRadius() + B.GetRadius());
				if (NewPhi < Contact.Phi)
				{
					TVector<T, d> Dir = Delta / DeltaLen;
					Contact.Phi = NewPhi;
					Contact.Normal = -Dir;
					Contact.Location = A1 + Dir * A.GetRadius();
				}
			}
		}

		template<typename T, int d>
		void ConstructSphereCapsuleConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<T, d>* Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TCapsule<T>* Object1 = Implicit1->template GetObject<const TCapsule<T> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

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
		void UpdateCapsuleCapsuleConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TPointContactManifold<T, d> & Contact = Constraint.Manifold;

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
				if (NewPhi < Contact.Phi)
				{
					TVector<T, d> Dir = Delta / DeltaLen;
					Contact.Phi = NewPhi;
					Contact.Normal = -Dir;
					Contact.Location = P1 + Dir * A.GetRadius();
				}
			}
		}

		template<typename T, int d>
		void ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			
			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const TCapsule<T> * Object1 = Implicit1->template GetObject<const TCapsule<T> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

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
		void UpdateCapsuleBoxConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TBox<T, d>& B, const TRigidTransform<T, d>& BTransform, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			// @todo(ccaulfield): Add custom capsule-box collision
			TPointContactManifold<T, d> & Contact = Constraint.Manifold;

			TRigidTransform<T, d> BToATransform = BTransform.GetRelativeTransform(ATransform);

			// Use GJK to track closest points (not strictly necessary yet)
			TVector<T, d> NearPointALocal, NearPointBLocal;
			T NearPointDistance;
			if (GJKDistance<T>(A, B, BToATransform, NearPointDistance, NearPointALocal, NearPointBLocal))
			{
				TVector<T, d> NearPointAWorld = ATransform.TransformPosition(NearPointALocal);
				TVector<T, d> NearPointBWorld = BTransform.TransformPosition(NearPointBLocal);
				TVector<T, d> NearPointBtoAWorld = NearPointAWorld - NearPointBWorld;
				Contact.Phi = NearPointDistance;
				Contact.Normal = NearPointBtoAWorld.GetSafeNormal();
				Contact.Location = NearPointAWorld;
			}
			else
			{
				// Use box particle samples against the implicit capsule
				const TArray<TVector<T, d>> BParticles = B.ComputeSamplePoints();
				const int32 NumParticles = BParticles.Num();
				for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
				{
					if (SampleObjectHelper(A, ATransform, BToATransform, BParticles[ParticleIndex], Thickness, Constraint))
					{
						// SampleObjectHelper expects A to be the box, so reverse the results
						Contact.Normal = -Contact.Normal;
					}
				}
			}

		}

		template<typename T, int d>
		void ConstructCapsuleBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			
			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

				UpdateCapsuleBoxConstraint(*Object0, Transform0, *Object1, Transform1, Thickness, *Constraint);

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
		void UpdateConvexConvexConstraint(const FImplicitObject& A, const TRigidTransform<T, d>& ATM, const FImplicitObject& B, const TRigidTransform<T, d>& BTM, const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);

			if (ensure(GetInnerType(A.GetType()) == ImplicitObjectType::Convex && GetInnerType(B.GetType()) == ImplicitObjectType::Convex))
			{
				const FConvex* AObject = nullptr;
				const FConvex* BObject = nullptr;

				if (IsScaled(A.GetType()) && IsScaled(B.GetType()))
				{
					AObject = static_cast<const FConvex*>(static_cast<const TImplicitObjectScaled<FConvex>&>(A).Object().Get());
					BObject = static_cast<const FConvex*>(static_cast<const TImplicitObjectScaled<FConvex>&>(B).Object().Get());
				}
				else if (A.GetType() == ImplicitObjectType::Transformed && B.GetType() == ImplicitObjectType::Transformed)
				{
					AObject = static_cast<const FConvex*>(static_cast<const TImplicitObjectTransformed<T, d>&>(A).Object().Get());
					BObject = static_cast<const FConvex*>(static_cast<const TImplicitObjectTransformed<T, d>&>(B).Object().Get());
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

		template<class T, int d>
		void ConstructConvexConvexConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			if (ensure(GetInnerType(Implicit0->GetType()) == ImplicitObjectType::Convex && GetInnerType(Implicit1->GetType()) == ImplicitObjectType::Convex))
			{
				FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;

				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);

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
		}


		//
		// Levelset-Levelset
		//

		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetLevelsetConstraint"), STAT_UpdateLevelsetLevelsetConstraint, STATGROUP_ChaosWide);
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateLevelsetLevelsetConstraint(const T Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint)
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


		template<typename T, int d>
		void ConstructLevelsetLevelsetConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;

			bool bIsParticleDynamic0 = Particle0->CastToRigidParticle() && Particle0->ObjectState() == EObjectStateType::Dynamic;
			if (!Particle1->Geometry() || (bIsParticleDynamic0 && !Particle0->CastToRigidParticle()->CollisionParticlesSize() && Particle0->Geometry() && !Particle0->Geometry()->IsUnderlyingUnion()))
			{
				Constraint->Particle[0] = Particle1;
				Constraint->Particle[1] = Particle0;
				Constraint->AddManifold(Implicit1, Implicit0);
			}
			else
			{
				Constraint->Particle[0] = Particle0;
				Constraint->Particle[1] = Particle1;
				Constraint->AddManifold(Implicit0, Implicit1);
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
		// Levelset-Union
		//

		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetUnionConstraint"), STAT_UpdateLevelsetUnionConstraint, STATGROUP_ChaosWide);
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateLevelsetUnionConstraint(const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetUnionConstraint);

			TGenericParticleHandle<T, d> Particle0 = Constraint.Particle[0];
			TGenericParticleHandle<T, d> Particle1 = Constraint.Particle[1];

			TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
			TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

			const FImplicitObject* ParticleObj = Particle0->Geometry().Get();
			const FImplicitObject* LevelsetObj = Particle1->Geometry().Get();

			if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
			{
				return;
			}

			if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
			{
				return;
			}

#if CHAOS_PARTICLEHANDLE_TODO
			TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes(LevelsetObj, LevelsetTM, *ParticleObj, ParticlesTM, Thickness);
			check(ParticleObj->IsUnderlyingUnion());
			const TImplicitObjectUnion<T, d>* UnionObj = static_cast<const TImplicitObjectUnion<T, d>*>(ParticleObj);
			for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticleObjPair : ParticleShapes)
			{
				const FImplicitObject* Object = ParticleObjPair.First;

				if (const int32* OriginalIdx = UnionObj->MCollisionParticleLookupHack.Find(Object))
				{
					const TBVHParticles<T, d>& SampleParticles = *InParticles.CollisionParticles(*OriginalIdx).Get();
					const TRigidTransform<T, d> ObjectTM = ParticleObjPair.Second * ParticlesTM;

					SampleObject2<UpdateType>(*LevelsetObj, LevelsetTM, SampleParticles, ObjectTM, Thickness, Constraint);
					if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
					{
						return;
					}
				}
			}
#endif
		}


		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateUnionLevelsetConstraint"), STAT_UpdateUnionLevelsetConstraint, STATGROUP_ChaosWide);
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateUnionLevelsetConstraint(const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateUnionLevelsetConstraint);

			TGenericParticleHandle<T, d> Particle0 = Constraint.Particle[0];
			TGenericParticleHandle<T, d> Particle1 = Constraint.Particle[1];

			TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
			TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

			if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
			{
				return;
			}

			if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
			{
				return;
			}

			const FImplicitObject* ParticleObj = Particle0->Geometry().Get();
			const FImplicitObject* LevelsetObj = Particle1->Geometry().Get();
			TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

			if (LevelsetShapes.Num() && Particle0->CollisionParticles().Get())
			{
				const TBVHParticles<T, d>& SampleParticles = *Particle0->CollisionParticles().Get();
				if (SampleParticles.Size())
				{
					for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
					{
						const FImplicitObject* Object = LevelsetObjPair.First;
						const TRigidTransform<T, d> ObjectTM = LevelsetObjPair.Second * LevelsetTM;
						// todo(brice) : SampleObject<UpdateType>(*Object, ObjectTM, SampleParticles, ParticlesTM, Thickness, Constraint);
						if (UpdateType == ECollisionUpdateType::Any && Constraint.GetPhi() < Thickness)
						{
							return;
						}
					}
				}
#if CHAOS_PARTICLEHANDLE_TODO
				else if (ParticleObj && ParticleObj->IsUnderlyingUnion())
				{
					const TImplicitObjectUnion<T, d>* UnionObj = static_cast<const TImplicitObjectUnion<T, d>*>(ParticleObj);
					//need to traverse shapes to get their collision particles
					for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
					{
						const FImplicitObject* LevelsetInnerObject = LevelsetObjPair.First;
						const TRigidTransform<T, d> LevelsetInnerObjectTM = LevelsetObjPair.Second * LevelsetTM;

						TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes(LevelsetInnerObject, LevelsetInnerObjectTM, *ParticleObj, ParticlesTM, Thickness);
						for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticleObjPair : ParticleShapes)
						{
							const FImplicitObject* ParticleInnerObject = ParticleObjPair.First;
							const TRigidTransform<T, d> ParticleInnerObjectTM = ParticleObjPair.Second * ParticlesTM;

							if (const int32* OriginalIdx = UnionObj->MCollisionParticleLookupHack.Find(ParticleInnerObject))
							{
								const TBVHParticles<T, d>& InnerSampleParticles = *InParticles.CollisionParticles(*OriginalIdx).Get();
								SampleObject2<UpdateType>(*LevelsetInnerObject, LevelsetInnerObjectTM, InnerSampleParticles, ParticleInnerObjectTM, Thickness, Constraint);
								if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
								{
									return;
								}
							}
						}

					}
				}
#endif
			}
		}

		//
		//  Union-Union
		//
		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateUnionUnionConstraint"), STAT_UpdateUnionUnionConstraint, STATGROUP_ChaosWide);

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateUnionUnionConstraint(const FImplicitObject& Implicit0, const TRigidTransform<T, d>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<T, d>& Transform1, const T Thickness, TCollisionConstraintBase<T, d>& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateUnionUnionConstraint);

			const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(&Implicit0, Transform0, Implicit1, Transform1, Thickness);
			for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
			{
				const FImplicitObject& LevelsetInnerObj = *LevelsetObjPair.First;
				const TRigidTransform<T, d>& LevelsetInnerObjTM = LevelsetObjPair.Second * Transform1;

				//now find all particle inner objects
				const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes(&LevelsetInnerObj, LevelsetInnerObjTM, Implicit0, Transform0, Thickness);

				//for each inner obj pair, update constraint
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticlePair : ParticleShapes)
				{
					const FImplicitObject& ParticleInnerObj = *ParticlePair.First;
					const TRigidTransform<T, d> ParticleInnerObjTM = ParticlePair.Second * Transform0;
					UpdateConstraintImp<UpdateType>(ParticleInnerObj, ParticleInnerObjTM, LevelsetInnerObj, LevelsetInnerObjTM, Thickness, Constraint);
				}
			}
		}



		template<typename T, int d>
		void ConstructUnionUnionConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateUnionUnionConstraint);

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
					ConstructConstraintsImpl<T, d>(Particle0, Particle1, ParticleInnerObj, LevelsetInnerObj, Transform0, Transform1, Thickness, NewConstraints);
				}
			}
		}

		//
		// Single-Union
		//

		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateSingleUnionConstraint"), STAT_UpdateSingleUnionConstraint, STATGROUP_ChaosWide);
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateSingleUnionConstraint(const FImplicitObject& Implicit0, const TRigidTransform<T, d>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<T, d>& Transform1, const T Thickness, TCollisionConstraintBase<T, d>& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateSingleUnionConstraint);

			const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(&Implicit0, Transform0, Implicit1, Transform1, Thickness);

			for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
			{
				const FImplicitObject& Implicit1InnerObj = *LevelsetObjPair.First;
				const TRigidTransform<T, d> Implicit1InnerObjTM = LevelsetObjPair.Second * Transform1;
				UpdateConstraintImp<UpdateType>(Implicit0, Transform0, Implicit1InnerObj, Implicit1InnerObjTM, Thickness, Constraint);
			}
		}



		template<typename T, int d>
		void ConstructSingleUnionConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			FRigidBodyPointContactConstraint* Constraint = new FRigidBodyPointContactConstraint;
			Constraint->Particle[0] = Particle0;
			Constraint->Particle[1] = Particle1;
			Constraint->AddManifold(Implicit0, Implicit1);

			UpdateSingleUnionConstraint<ECollisionUpdateType::Any>(*Implicit0, Transform0, *Implicit1, Transform1, Thickness, *Constraint);

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
		// Constraint API
		//

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateConstraintImp(const FImplicitObject& Implicit0, const TRigidTransform<T, d>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<T, d>& Transform1, const T Thickness, TCollisionConstraintBase<T, d>& ConstraintBase)
		{
			EImplicitObjectType Implicit0Type = GetInnerType(Implicit0.GetType());
			EImplicitObjectType Implicit1Type = GetInnerType(Implicit1.GetType());

			if (Implicit0Type == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0.template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform0 = TransformedImplicit0->GetTransform() * Transform0;
				UpdateConstraintImp<UpdateType>(*TransformedImplicit0->GetTransformedObject(), TransformedTransform0, Implicit1, Transform1, Thickness, ConstraintBase);
			}
			else if (Implicit1Type == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1.template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform1 = TransformedImplicit1->GetTransform() * Transform1;
				UpdateConstraintImp<UpdateType>(Implicit0, Transform0, *TransformedImplicit1->GetTransformedObject(), TransformedTransform1, Thickness, ConstraintBase);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateBoxBoxConstraint(*Implicit0.template GetObject<TBox<T, d>>(), Transform0, *Implicit1.template GetObject<TBox<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				UpdateSphereSphereConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TSphere<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d>>());
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateBoxPlaneConstraint(*Implicit0.template GetObject<TBox<T, d>>(), Transform0, *Implicit1.template GetObject<TPlane<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateSpherePlaneConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TPlane<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateSphereBoxConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TBox<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateSphereCapsuleConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateCapsuleCapsuleConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateCapsuleBoxConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject<TBox<T, d>>(), Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d>>());
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				TRigidBodyPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >();
				TRigidBodyPointContactConstraint<T, d> TmpConstraint = Constraint;
				UpdateBoxPlaneConstraint(*Implicit1.template GetObject<TBox<T, d>>(), Transform1, *Implicit0.template GetObject<TPlane<T, d>>(), Transform0, Thickness, TmpConstraint);
				if (TmpConstraint.GetPhi() < Constraint.GetPhi())
				{
					Constraint = TmpConstraint;
					Constraint.SetNormal(-Constraint.GetNormal());
				}
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
				UpdateSphereBoxConstraint(*Implicit1.template GetObject<TSphere<T, d>>(), Transform1, *Implicit0.template GetObject<TBox<T, d>>(), Transform0, Thickness, TmpConstraint);
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
				UpdateCapsuleBoxConstraint(*Implicit1.template GetObject<TCapsule<T>>(), Transform1, *Implicit0.template GetObject<TBox<T, d>>(), Transform0, Thickness, TmpConstraint);
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
			else if (Implicit0Type < TImplicitObjectUnion<T, d>::StaticType() && Implicit1Type == TImplicitObjectUnion<T, d>::StaticType())
			{
				return UpdateSingleUnionConstraint<UpdateType>(Implicit0, Transform0, Implicit1, Transform1, Thickness, ConstraintBase);
			}
			else if (Implicit0Type == TImplicitObjectUnion<T, d>::StaticType() && Implicit1Type < TImplicitObjectUnion<T, d>::StaticType())
			{
				check(false);	//should not be possible to get this ordering (see ComputeConstraint)
			}
			else if (Implicit0Type == TImplicitObjectUnion<T, d>::StaticType() && Implicit1Type == TImplicitObjectUnion<T, d>::StaticType())
			{
				return UpdateUnionUnionConstraint<UpdateType>(Implicit0, Transform0, Implicit1, Transform1, Thickness, ConstraintBase);
			}
			else if (Implicit0.IsConvex() && Implicit1.IsConvex())
			{
				UpdateConvexConvexConstraint(Implicit0, Transform0, Implicit1, Transform1, Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit1.IsUnderlyingUnion())
			{
				UpdateUnionLevelsetConstraint<UpdateType>(Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d> >());
			}
			else if (Implicit0.IsUnderlyingUnion())
			{
				UpdateLevelsetUnionConstraint<UpdateType>(Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d> >());
			}
			else
			{
				UpdateLevelsetLevelsetConstraint<UpdateType>(Thickness, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T,d> >());
			}
		}

		template<typename T, int d>
		void ConstructPairConstraintImpl(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			const TPerShapeData<T, d>* Shape0 = Particle0->GetImplicitShape(Implicit0);
			const TPerShapeData<T, d>* Shape1 = Particle1->GetImplicitShape(Implicit1);

			// If either shape is disabled for collision bail without constructing a constraint
			if((Shape0 && Shape0->bDisable) || (Shape1 && Shape1->bDisable))
			{
				return;
			}

			// See if we already have a constraint for this shape pair
			// todo(brice) : Not sure this actually prevents duplicate constraints.
			for (int32 i = 0; i < NewConstraints.Num(); i++)
			{
				if (NewConstraints[i]->GetType() == TRigidBodyPointContactConstraint<T, d>::StaticType())
				{
					if (NewConstraints[i]->As< TRigidBodyPointContactConstraint<T, d> >()->ContainsManifold(Implicit0, Implicit1))
					{
						return;
					}
				}
				else if (NewConstraints[i]->GetType() == TRigidBodyPlaneContactConstraint<T, d>::StaticType())
				{
					if (NewConstraints[i]->As< TRigidBodyPlaneContactConstraint<T, d> >()->ContainsManifold(Implicit0, Implicit1))
					{
						return;
					}
				}
			}

			if (!Implicit0 || !Implicit1)
			{
				ConstructLevelsetLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
				return;
			}
			
			EImplicitObjectType Implicit0Type = GetInnerType(Implicit0->GetType());
			EImplicitObjectType Implicit1Type = GetInnerType(Implicit1->GetType());
			if (Implicit0Type == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform0 = TransformedImplicit0->GetTransform() * Transform0;
				ConstructPairConstraintImpl(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), Implicit1, TransformedTransform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit1Type == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform1 = TransformedImplicit1->GetTransform() * Transform1;
				ConstructPairConstraintImpl(Particle0, Particle1, Implicit0, TransformedImplicit1->GetTransformedObject(), Transform0, TransformedTransform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereSphereConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				ConstructBoxPlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxPlaneConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
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
				ConstructCapsuleCapsuleConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructCapsuleBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TBox<T,d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructCapsuleBoxConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type < TImplicitObjectUnion<T, d>::StaticType() && Implicit1Type == TImplicitObjectUnion<T, d>::StaticType())
			{
				ConstructSingleUnionConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TImplicitObjectUnion<T, d>::StaticType() && Implicit1Type < TImplicitObjectUnion<T, d>::StaticType())
			{
				ConstructSingleUnionConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, Thickness, NewConstraints);
			}
			else if (Implicit0Type == TImplicitObjectUnion<T, d>::StaticType() && Implicit1Type == TImplicitObjectUnion<T, d>::StaticType())
			{
				// Union-union creates multiple manifolds - we should never get here for this pair type. See ConstructConstraintsImpl and ConstructUnionUnionConstraints
				ensure(false);
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

		template<typename T, int d>
		void ConstructConstraintsImpl(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T Thickness, FCollisionConstraintsArray& NewConstraints)
		{
			// TriangleMesh implicits are for scene query only.
			if (Implicit0 && GetInnerType(Implicit0->GetType()) == ImplicitObjectType::TriangleMesh) return;
			if (Implicit1 && GetInnerType(Implicit1->GetType()) == ImplicitObjectType::TriangleMesh) return;

			if (Implicit0 && Implicit0->GetType() == TImplicitObjectUnion<T, d>::StaticType() &&
				Implicit1 && Implicit1->GetType() == TImplicitObjectUnion<T, d>::StaticType())
			{
				return ConstructUnionUnionConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
			else
			{
				return ConstructPairConstraintImpl(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, Thickness, NewConstraints);
			}
		}



		template void UpdateBoxBoxConstraint<float, 3>(const TBox<float, 3>& Box1, const TRigidTransform<float, 3>& Box1Transform, const TBox<float, 3>& Box2, const TRigidTransform<float, 3>& Box2Transform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructBoxBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template bool UpdateBoxPlaneConstraint<float, 3>(const TBox<float, 3>& Box, const TRigidTransform<float, 3>& BoxTransform, const TPlane<float, 3>& Plane, const TRigidTransform<float, 3>& PlaneTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructBoxPlaneConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereSphereConstraint<float, 3>(const TSphere<float, 3>& Sphere1, const TRigidTransform<float, 3>& Sphere1Transform, const TSphere<float, 3>& Sphere2, const TRigidTransform<float, 3>& Sphere2Transform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructSphereSphereConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSpherePlaneConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TPlane<float, 3>& Plane, const TRigidTransform<float, 3>& PlaneTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructSpherePlaneConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereBoxConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TBox<float, 3>& Box, const TRigidTransform<float, 3>& BoxTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructSphereBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereCapsuleConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TCapsule<float>& Box, const TRigidTransform<float, 3>& BoxTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructSphereCapsuleConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateCapsuleCapsuleConstraint<float, 3>(const TCapsule<float>& A, const TRigidTransform<float, 3>& ATransform, const TCapsule<float>& B, const TRigidTransform<float, 3>& BTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateCapsuleBoxConstraint<float, 3>(const TCapsule<float>& A, const TRigidTransform<float, 3>& ATransform, const TBox<float, 3>& B, const TRigidTransform<float, 3>& BTransform, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructCapsuleBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateConvexConvexConstraint<float, 3>(const FImplicitObject& A, const TRigidTransform<float, 3>& ATM, const FImplicitObject& B, const TRigidTransform<float, 3>& BTM, const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructConvexConvexConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Any, float, 3>(const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest, float, 3>(const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void ConstructLevelsetLevelsetConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateLevelsetUnionConstraint<ECollisionUpdateType::Any, float, 3>(const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateLevelsetUnionConstraint<ECollisionUpdateType::Deepest, float, 3>(const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);

		template void UpdateUnionLevelsetConstraint<ECollisionUpdateType::Any, float, 3>(const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateUnionLevelsetConstraint<ECollisionUpdateType::Deepest, float, 3>(const float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);

		template void UpdateUnionUnionConstraint< ECollisionUpdateType::Any, float, 3>(const FImplicitObject& Implicit0, const TRigidTransform<float, 3>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<float, 3>& Transform1, const float Thickness, TCollisionConstraintBase<float, 3>& Constraint);
		template void UpdateUnionUnionConstraint< ECollisionUpdateType::Deepest, float, 3>(const FImplicitObject& Implicit0, const TRigidTransform<float, 3>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<float, 3>& Transform1, const float Thickness, TCollisionConstraintBase<float, 3>& Constraint);
		template void ConstructUnionUnionConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSingleUnionConstraint< ECollisionUpdateType::Any, float, 3>(const FImplicitObject& Implicit0, const TRigidTransform<float, 3>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<float, 3>& Transform1, const float Thickness, TCollisionConstraintBase<float, 3>& Constraint);
		template void UpdateSingleUnionConstraint< ECollisionUpdateType::Deepest, float, 3>(const FImplicitObject& Implicit0, const TRigidTransform<float, 3>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<float, 3>& Transform1, const float Thickness, TCollisionConstraintBase<float, 3>& Constraint);
		template void ConstructSingleUnionConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

		template void UpdateConstraintImp<ECollisionUpdateType::Any, float, 3>(const FImplicitObject& Implicit0, const TRigidTransform<float, 3>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<float, 3>& Transform1, const float Thickness, TCollisionConstraintBase<float, 3>& Constraints);
		template void UpdateConstraintImp<ECollisionUpdateType::Deepest, float, 3>(const FImplicitObject& Implicit0, const TRigidTransform<float, 3>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<float, 3>& Transform1, const float Thickness, TCollisionConstraintBase<float, 3>& Constraint);

		template void ConstructPairConstraintImpl<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);
		template void ConstructConstraintsImpl<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float Thickness, FCollisionConstraintsArray& NewConstraints);

	} // Collisions

} // Chaos
