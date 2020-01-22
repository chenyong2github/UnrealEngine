// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/GeometryQueries.h"

DECLARE_CYCLE_STAT(TEXT("Collisions::GJK"), STAT_Collisions_GJK, STATGROUP_ChaosCollision);

//#pragma optimize("", off)

bool Chaos_Collision_UseManifolds = true;
FAutoConsoleVariableRef CVarChaosCollisionUseManifolds(TEXT("p.Chaos.Collision.UseManifolds"), Chaos_Collision_UseManifolds, TEXT("Enable/Disable use of manifoldes in collision."));

bool Chaos_Collision_UseManifolds_Test = false;
FAutoConsoleVariableRef CVarChaosCollisionUseManifoldsTest(TEXT("p.Chaos.Collision.UseManifoldsTest"), Chaos_Collision_UseManifolds_Test, TEXT("Enable/Disable use of manifoldes in collision."));


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

			TContactPoint() 
			: Normal(1, 0, 0)
			, Phi(TNumericLimits<T>::Max()) {}
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

		/**
		 * Select the best manifold point as the new contact point and update the constraint data.
		 */
		template<class T, int d>
		void UpdateConstraintFromManifold(TRigidBodyMultiPointContactConstraint<T, d>& Constraint, const TRigidTransform<T, d>& ParticleTransform0, const TRigidTransform<T, d>& ParticleTransform1, const T CullDistance)
		{
			// TEMP: Test code - run single-point algorithm instead. Allows us to debug draw the manifold without using it
			if (Chaos_Collision_UseManifolds_Test)
			{
				TRigidBodyPointContactConstraint<T, d> PointConstraint(Constraint.Particle[0], Constraint.Manifold.Implicit[0], Constraint.ImplicitTransform[0], Constraint.Particle[1], Constraint.Manifold.Implicit[1], Constraint.ImplicitTransform[1], Constraint.Manifold.ShapesType);
				UpdateConstraint<ECollisionUpdateType::Deepest>(PointConstraint, ParticleTransform0, ParticleTransform1, CullDistance);
				Constraint.Manifold = PointConstraint.Manifold;
				return;
			}

			// Get the plane and point transforms (depends which body owns the plane)
			const FRigidTransform3 Transform0 = Constraint.ImplicitTransform[0] * ParticleTransform0;
			const FRigidTransform3 Transform1 = Constraint.ImplicitTransform[1] * ParticleTransform1;
			const FRigidTransform3& PlaneTransform = (Constraint.GetManifoldPlaneOwnerIndex() == 0) ? Transform0 : Transform1;
			const FRigidTransform3& PointsTransform = (Constraint.GetManifoldPlaneOwnerIndex() == 0) ? Transform1 : Transform0;

			// World-space manifold plane
			FVec3 PlaneNormal = PlaneTransform.TransformVector(Constraint.GetManifoldPlaneNormal());
			FVec3 PlanePos = PlaneTransform.TransformPosition(Constraint.GetManifoldPlanePosition());

			// Select the best manifold point
			const int32 NumPoints = Constraint.NumManifoldPoints();
			for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
			{
				// World-space manifold point and distance to manifold plane
				FVec3 PointPos = PointsTransform.TransformPosition(Constraint.GetManifoldPoint(PointIndex));
				FReal PointDistance = FVec3::DotProduct(PointPos - PlanePos, PlaneNormal);

				// If this is the deepest hit, select it
				if (PointDistance < Constraint.Manifold.Phi)
				{
					// @todo(chaos): Consider using average of plane and point positions for contact location
					FVec3 ContactPos = PointPos - PointDistance * PlaneNormal;
					FVec3 ContactNormal = (Constraint.GetManifoldPlaneOwnerIndex() == 0) ? -PlaneNormal : PlaneNormal;
					Constraint.Manifold.Phi = PointDistance;
					Constraint.Manifold.Location = ContactPos;
					Constraint.Manifold.Normal = ContactNormal;
				}
			}
		}


		template <typename T, int d, typename GeometryA, typename GeometryB>
		TContactPoint<T> GJKContactPoint(const GeometryA& A, const TRigidTransform<T, d>& ATM, const GeometryB& B, const TRigidTransform<T, d>& BTM, const TVector<T, 3>& InitialDir)
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_GJK);

			TContactPoint<T> Contact;
			const TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);

			T Penetration;
			TVec3<T> ClosestA, ClosestB, Normal;
			int32 NumIterations = 0;

			if (ensure(GJKPenetration<true>(A, B, BToATM, Penetration, ClosestA, ClosestB, Normal, (T)0, InitialDir, (T)0, &NumIterations)))
			{
				Contact.Location = ATM.TransformPosition(ClosestA);
				Contact.Normal = -ATM.TransformVector(Normal);
				Contact.Phi = -Penetration;
			}

			//static float AverageIterations = 0;
			//AverageIterations = AverageIterations + ((float)NumIterations - AverageIterations) / 1000.0f;
			//UE_LOG(LogChaos, Warning, TEXT("GJK Its: %f"), AverageIterations);

			return Contact;
		}

		template <typename GeometryA, typename GeometryB, typename T, int d>
		TContactPoint<T> GJKImplicitContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const GeometryB& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			TContactPoint<T> Contact;
			const TRigidTransform<T, d> AToBTM = ATransform.GetRelativeTransform(BTransform);

			T Penetration = FLT_MAX;
			TVec3<T> Location, Normal;
			if (const TImplicitObjectScaled<GeometryA>* ScaledConvexImplicit = A.template GetObject<const TImplicitObjectScaled<GeometryA> >())
			{
				if (B.GJKContactPoint(*ScaledConvexImplicit, AToBTM, CullDistance, Location, Normal, Penetration))
				{
					Contact.Phi = Penetration;
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVector(Normal);
				}
			}
			else if (const TImplicitObjectInstanced<GeometryA>* InstancedConvexImplicit = A.template GetObject<const TImplicitObjectInstanced<GeometryA> >())
			{
				if (const GeometryA * InstancedInnerObject = static_cast<const GeometryA*>(InstancedConvexImplicit->GetInstancedObject()))
				{
					if (B.GJKContactPoint(*InstancedInnerObject, AToBTM, CullDistance, Location, Normal, Penetration))
					{
						Contact.Phi = Penetration;
						Contact.Location = BTransform.TransformPosition(Location);
						Contact.Normal = BTransform.TransformVector(Normal);
					}
				}
			}
			else if (const GeometryA* ConvexImplicit = A.template GetObject<const GeometryA>())
			{
				if (B.GJKContactPoint(*ConvexImplicit, AToBTM, CullDistance, Location, Normal, Penetration))
				{
					Contact.Phi = Penetration;
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVector(Normal);
				}
			}

			return Contact;
		}


		template<class T, int d>
		TContactPoint<T> ConvexConvexContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATM, const FImplicitObject& B, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			return CastHelper(A, ATM, [&](const auto& ADowncast, const TRigidTransform<T,d>& AFullTM)
			{
				return CastHelper(B, BTM, [&](const auto& BDowncast, const TRigidTransform<T,d>& BFullTM)
				{
					return GJKContactPoint(ADowncast, AFullTM, BDowncast, BFullTM, TVector<T, d>(1, 0, 0));
				});
			});
		}

		template <typename T, int d>
		void UpdateSingleShotManifold(TRigidBodyMultiPointContactConstraint<T, d>&  Constraint, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance)
		{
			// single shot manifolds for TConvex implicit object in the constraints implicit[0] position. 
			TContactPoint<T> ContactPoint = ConvexConvexContactPoint(*Constraint.Manifold.Implicit[0], Transform0, *Constraint.Manifold.Implicit[1], Transform1, CullDistance);

			TArray<FVec3> CollisionSamples;
			//
			//  @todo(chaos) : Collision Manifold
			//   Remove the dependency on the virtual calls on the Implicit. Don't use FindClosestFaceAndVertices
			//   this relies on virtual calls on the ImplicitObject. Instead pass a parameters structures into 
			//   ConvexConvexContactPoint that can collect the face indices during evaluation of the support functions. 
			//   This can be implemented without virtual calls.
			//
			int32 FaceIndex = Constraint.Manifold.Implicit[0]->FindClosestFaceAndVertices(Transform0.InverseTransformPosition(ContactPoint.Location), CollisionSamples, 1.f);

			bool bNewManifold = (FaceIndex != Constraint.GetManifoldPlaneFaceIndex()) || (Constraint.NumManifoldPoints() == 0);
			if (bNewManifold)
			{
				const FVec3 PlaneNormal = Transform1.InverseTransformVector(ContactPoint.Normal);
				const FVec3 PlanePos = Transform1.InverseTransformPosition(ContactPoint.Location - ContactPoint.Phi*ContactPoint.Normal);
				Constraint.SetManifoldPlane(1, FaceIndex, PlaneNormal, PlanePos);

				//
				// @todo(chaos) : Collision Manifold
				//   Only save the four best samples and hard-code the size of Constraint.Samples to [len:4].
				//   Currently this just grabs all points and uses the deepest point for resolution. 
				//
				Constraint.ResetManifoldPoints(CollisionSamples.Num());
				for (FVec3 Sample : CollisionSamples)
				{
					Constraint.AddManifoldPoint(Sample);
				}
			}
		}

		template <typename T, int d>
		void UpdateIterativeManifold(TRigidBodyMultiPointContactConstraint<T, d>&  Constraint, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance)
		{
			auto SumSampleData = [&](TRigidBodyMultiPointContactConstraint<T, d>& LambdaConstraint) -> TVector<float, 3>
			{
				TVector<float, 3> ReturnValue(0);
				for (int i = 0; i < LambdaConstraint.NumManifoldPoints(); i++)
				{
					ReturnValue += LambdaConstraint.GetManifoldPoint(i);
				}
				return ReturnValue;
			};

			// iterative manifolds for non TConvex implicit objects that require sampling 
			TContactPoint<T> ContactPoint = ConvexConvexContactPoint(*Constraint.Manifold.Implicit[0], Transform0, *Constraint.Manifold.Implicit[1], Transform1, CullDistance);

			if (!ContactPoint.Normal.Equals(Constraint.GetManifoldPlaneNormal()) || !Constraint.NumManifoldPoints())
			{
				Constraint.ResetManifoldPoints();
				FVec3 PlaneNormal = Transform1.InverseTransformVector(ContactPoint.Normal);
				FVec3 PlanePosition = Transform1.InverseTransformPosition(ContactPoint.Location - ContactPoint.Phi*ContactPoint.Normal);
				Constraint.SetManifoldPlane(1, INDEX_NONE, PlaneNormal, PlanePosition);
			}

			TVector<T, d> SurfaceSample = Transform0.InverseTransformPosition(ContactPoint.Location);
			if (Constraint.NumManifoldPoints() < 4)
			{
				Constraint.AddManifoldPoint(SurfaceSample);
			}
			else if (Constraint.NumManifoldPoints() == 4)
			{
				TVector<T, d> Center = SumSampleData(Constraint) / Constraint.NumManifoldPoints();
				T Delta = (Center - SurfaceSample).SizeSquared();

				//
				// @todo(chaos) : Collision Manifold
				//    The iterative manifold need to be maximized for area instead of largest 
				//    distance from center.
				//
				T SmallestDelta = FLT_MAX;
				int32 SmallestIndex = 0;
				for (int32 idx = 0; idx < Constraint.NumManifoldPoints(); idx++)
				{
					T IdxDelta = (Center - Constraint.GetManifoldPoint(idx)).SizeSquared();
					if (IdxDelta < SmallestDelta)
					{
						SmallestDelta = IdxDelta;
						SmallestIndex = idx;
					}
				}

				if (Delta > SmallestDelta) 
				{
					Constraint.SetManifoldPoint(SmallestIndex, SurfaceSample);
				}
			}
			else
			{
				ensure(false); // max of 4 points
			}
		}

		template <typename TPGeometryClass>
		const TPGeometryClass* GetInnerObject(const FImplicitObject& Geometry)
		{
			if (const TImplicitObjectScaled<TPGeometryClass>* ScaledConvexImplicit = Geometry.template GetObject<const TImplicitObjectScaled<TPGeometryClass> >())
				return (Geometry.template GetObject<const TImplicitObjectScaled<TPGeometryClass> >())->GetUnscaledObject();
			else if (const TImplicitObjectInstanced<TPGeometryClass>* InstancedImplicit = Geometry.template GetObject<const TImplicitObjectInstanced<TPGeometryClass> >())
				return (Geometry.template GetObject<const TImplicitObjectInstanced<TPGeometryClass> >())->GetInstancedObject();
			else if (const TPGeometryClass* ConvexImplicit = Geometry.template GetObject<const TPGeometryClass>())
				return Geometry.template GetObject<const TPGeometryClass>();
			return nullptr;
		}


		//
		// Box - Box
		//
		template <typename T, int d>
		TContactPoint<T> BoxBoxContactPoint(const TAABB<T, d>& Box1, const TRigidTransform<T, d>& ATM, const TAABB<T, d>& Box2, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			return GJKContactPoint(Box1, ATM, Box2, BTM, TVector<T, d>(1, 0, 0));
		}

		template <typename T, int d>
		void UpdateBoxBoxConstraint(const TAABB<T, d>& Box1, const TRigidTransform<T, d>& Box1Transform, const TAABB<T, d>& Box2, const TRigidTransform<T, d>& Box2Transform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, BoxBoxContactPoint(Box1, Box1Transform, Box2, Box2Transform, CullDistance));
		}

		template <typename T, int d>
		void UpdateBoxBoxManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructBoxBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateBoxBoxConstraint(Object0->BoundingBox(), Transform0, Object1->BoundingBox(), Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Box - HeightField
		//

		template <typename T, int d>
		TContactPoint<T> BoxHeightFieldContactPoint(const TAABB<T, d>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TBox<float, 3> >(TBox<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d>
		void UpdateBoxHeightFieldConstraint(const TAABB<T, d>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, BoxHeightFieldContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		template<class T, int d>
		void UpdateBoxHeightFieldManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructBoxHeightFieldConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateBoxHeightFieldConstraint(Object0->BoundingBox(), Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}



		//
		// Box-Plane
		//

		template <typename T, int d>
		void UpdateBoxPlaneConstraint(const TAABB<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			TCollisionContact<T, d> & Contact = Constraint.Manifold;

#if USING_CODE_ANALYSIS
			MSVC_PRAGMA(warning(push))
				MSVC_PRAGMA(warning(disable : ALL_CODE_ANALYSIS_WARNINGS))
#endif	// USING_CODE_ANALYSIS

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
		}

		template<class T, int d>
		void UpdateBoxPlaneManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}


		template<typename T, int d>
		void ConstructBoxPlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const TPlane<T, d> * Object1 = Implicit1->template GetObject<const TPlane<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateBoxPlaneConstraint(Object0->BoundingBox(), Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Box-TriangleMesh
		//

		template <typename T, int d>
		TContactPoint<T> BoxTriangleMeshContactPoint(const TAABB<T, d>& A, const TRigidTransform<T, d>& ATransform, const FTriangleMeshImplicitObject& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TBox<float, 3> >(TBox<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d>
		void UpdateBoxTriangleMeshConstraint(const TAABB<T, d>& Box0, const TRigidTransform<T, d>& Transform0, const FTriangleMeshImplicitObject& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, BoxTriangleMeshContactPoint(Box0, Transform0, TriangleMesh1, Transform1, CullDistance));
		}


		template <typename T, int d>
		void UpdateBoxTriangleMeshManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{

		}

		template<typename T, int d>
		void ConstructBoxTriangleMeshConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TBox<T, d> * Object0 = Implicit0->template GetObject<const TBox<T, d> >();
			const FTriangleMeshImplicitObject * Object1 = GetInnerObject<FTriangleMeshImplicitObject>(*Implicit1);
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateBoxTriangleMeshConstraint(Object0->GetAABB(), Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Sphere - Sphere
		//

		template <typename T, int d>
		TContactPoint<T> SphereSphereContactPoint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, const T CullDistance)
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
		void UpdateSphereSphereConstraint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereSphereContactPoint(Sphere1, Sphere1Transform, Sphere2, Sphere2Transform, CullDistance));
		}

		template<class T, int d>
		void UpdateSphereSphereManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSphereSphereConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TSphere<T, d> * Object1 = Implicit1->template GetObject<const TSphere<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateSphereSphereConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Sphere - HeightField
		//

		template <typename T, int d>
		TContactPoint<T> SphereHeightFieldContactPoint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TSphere<float, 3> >(TSphere<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d>
		void UpdateSphereHeightFieldConstraint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereHeightFieldContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		template<class T, int d>
		void UpdateSphereHeightFieldManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSphereHeightFieldConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateSphereHeightFieldConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		//  Sphere-Plane
		//

		template <typename T, int d>
		void UpdateSpherePlaneConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
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
		void UpdateSpherePlaneManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSpherePlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TPlane<T, d> * Object1 = Implicit1->template GetObject<const TPlane<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateSpherePlaneConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Sphere - Box
		//

		template <typename T, int d>
		TContactPoint<T> SphereBoxContactPoint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TAABB<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const T CullDistance)
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
		void UpdateSphereBoxConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TAABB<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereBoxContactPoint(Sphere, SphereTransform, Box, BoxTransform, CullDistance));
		}

		template<class T, int d>
		void UpdateSphereBoxManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSphereBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateSphereBoxConstraint(*Object0, Transform0, Object1->BoundingBox(), Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}


		//
		// Sphere - Capsule
		//

		template <typename T, int d>
		TContactPoint<T> SphereCapsuleContactPoint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
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
		void UpdateSphereCapsuleConstraint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereCapsuleContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		template<class T, int d>
		void UpdateSphereCapsuleManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructSphereCapsuleConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<T, d>* Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const TCapsule<T>* Object1 = Implicit1->template GetObject<const TCapsule<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateSphereCapsuleConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Sphere-TriangleMesh
		//

		template <typename T, int d>
		TContactPoint<T> SphereTriangleMeshContactPoint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const FTriangleMeshImplicitObject& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TSphere<float, 3> >(TSphere<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d>
		void UpdateSphereTriangleMeshConstraint(const TSphere<T, d>& Sphere0, const TRigidTransform<T, d>& Transform0, const FTriangleMeshImplicitObject& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereTriangleMeshContactPoint(Sphere0, Transform0, TriangleMesh1, Transform1, CullDistance));
		}


		template <typename T, int d>
		void UpdateSphereTriangleMeshManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{

		}

		template<typename T, int d>
		void ConstructSphereTriangleMeshConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<T, d> * Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			const FTriangleMeshImplicitObject * Object1 = GetInnerObject<FTriangleMeshImplicitObject>(*Implicit1);
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateSphereTriangleMeshConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}



		//
		// Capsule-Capsule
		//

		template <typename T, int d>
		TContactPoint<T> CapsuleCapsuleContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
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
		void UpdateCapsuleCapsuleConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleCapsuleContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		template<class T, int d>
		void UpdateCapsuleCapsuleManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const TCapsule<T> * Object1 = Implicit1->template GetObject<const TCapsule<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateCapsuleCapsuleConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Capsule - Box
		//

		template <typename T, int d>
		TContactPoint<T> CapsuleBoxContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TAABB<T, d>& B, const TRigidTransform<T, d>& BTransform, const TVector<T, 3>& InitialDir, const T CullDistance)
		{
			return GJKContactPoint(A, ATransform, B, BTransform, InitialDir);
		}

		template <typename T, int d>
		void UpdateCapsuleBoxConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TAABB<T, d>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			const TVector<T, d> InitialDir = ATransform.GetRotation().Inverse() * -Constraint.GetNormal();
			UpdateContactPoint(Constraint.Manifold, CapsuleBoxContactPoint(A, ATransform, B, BTransform, InitialDir, CullDistance));
		}

		/**
		 * Generate a Capsule-Box Manifold.
		 *
		 * This starts by finding the closest feature using GJK/EPA. Then it adds points to the manifold that depend on
		 * the closest feature types on the box (face, edge, vertex) and capsule (edge, vertex).
		 *
		 * For box vertex collisions, only the single near point/plane is used in the manifold.
		 * For box face collisions, the box owns the manifold plane, and the capsule edge nearest the box face is used to generate manifold points.
		 * For box edge collision with the capsule edge, the capsule owns the manifold plane, and the capsule edge is projected onto the box plane to generate the manifold points.
		 * For box edge with capsule vertex collisions, only the single near point/plane is used in the manifold.
		 * 
		 */
		template<class T, int d>
		void UpdateCapsuleBoxManifold(const TCapsule<T>& Capsule, const TRigidTransform<T, d>& CapsuleTM, const TAABB<T, d>& Box, const TRigidTransform<T, d>& BoxTM, const T CullDistance, FRigidBodyMultiPointContactConstraint& Constraint)
		{
			Constraint.ResetManifoldPoints();

			// Find the nearest points on the capsule and box
			// Note: We flip the order for GJK so we get the normal in box space. This makes it easier to build the face-capsule manifold.
			const TRigidTransform<T, d> CapsuleToBoxTM = CapsuleTM.GetRelativeTransform(BoxTM);

			// NOTE: All GJK results in box-space
			// @todo(ccaulfield): use center-to-center direction for InitialDir
			FVec3 InitialDir = FVec3(1, 0, 0);
			T Penetration;
			FVec3 CapsuleClosestBoxSpace, BoxClosestBoxSpace, NormalBoxSpace;
			{
				SCOPE_CYCLE_COUNTER(STAT_Collisions_GJK);
				if (!ensure(GJKPenetration<true>(Box, Capsule, CapsuleToBoxTM, Penetration, BoxClosestBoxSpace, CapsuleClosestBoxSpace, NormalBoxSpace, (T)0, InitialDir, (T)0)))
				{
					return;
				}
			}

			// Cache the closest point so we don't need to re-iterate over the manifold on the first iteration
			Constraint.Manifold.Location = BoxTM.TransformPosition(BoxClosestBoxSpace);
			Constraint.Manifold.Normal = BoxTM.TransformVector(NormalBoxSpace);
			Constraint.Manifold.Phi = -Penetration;

			// Find the box feature that the near point is on
			// Face, Edge, or Vertex can be determined from number of non-zero elements in the box-space normal.
			const FReal ComponentEpsilon = 0.09f; // ~Sin(5deg) = angle tolerance
			int32 NumNonZeroNormalComponents = 0;
			int32 MaxComponentIndex = INDEX_NONE;
			FReal MaxComponentValue = 0;
			for (int32 ComponentIndex = 0; ComponentIndex < 3; ++ComponentIndex)
			{
				FReal AbsComponentValue = FMath::Abs(NormalBoxSpace[ComponentIndex]);
				if (AbsComponentValue > ComponentEpsilon)
				{
					++NumNonZeroNormalComponents;
				}
				if (AbsComponentValue > MaxComponentValue)
				{
					MaxComponentValue = AbsComponentValue;
					MaxComponentIndex = ComponentIndex;
				}
			}

			// Make sure we actually have a feature to use
			if (!ensure(MaxComponentIndex != INDEX_NONE))
			{
				return;
			}

			FVec3 CapsuleAxis = CapsuleToBoxTM.TransformVector(Capsule.GetAxis()); // Box space capsule axis
			FReal CapsuleAxisNormal = FVec3::DotProduct(CapsuleAxis, NormalBoxSpace);
			const bool bIsBoxFaceContact = (NumNonZeroNormalComponents == 1);
			const bool bIsBoxEdgeContact = (NumNonZeroNormalComponents == 2);
			const bool bIsBoxVertexContact = (NumNonZeroNormalComponents == 3);
			const bool bIsCapsuleEdgeContact = (CapsuleAxisNormal < ComponentEpsilon);
			const bool bIsCapsuleVertexContact = !bIsCapsuleEdgeContact;

			// We just use the nearest point for these combinations, with the box as the plane owner
			if (bIsBoxVertexContact || (bIsBoxEdgeContact && bIsCapsuleVertexContact))
			{
				Constraint.SetManifoldPlane(1, INDEX_NONE, NormalBoxSpace, BoxClosestBoxSpace);
				Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleClosestBoxSpace));
				return;
			}

			// Get the radial direction from the capsule axis to the box face
			FVec3 CapsuleRadiusDir = FVec3::CrossProduct(FVec3::CrossProduct(NormalBoxSpace, CapsuleAxis), CapsuleAxis);
			if (!Utilities::NormalizeSafe(CapsuleRadiusDir))
			{
				// Capsule is perpendicular to the face, just leave the one-point manifold
				Constraint.SetManifoldPlane(1, INDEX_NONE, NormalBoxSpace, BoxClosestBoxSpace);
				Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleClosestBoxSpace));
				return;
			}

			// This is the axis of the box face we will be using below
			int32 FaceAxisIndex = MaxComponentIndex;

			// If this is a box face contact, the box is the manifold plane owner, regardless of the capsule feature involved. 
			// We take the edge of the capsule nearest the face, clip it to the face, and use the clipped points as the manifold points.
			// NOTE: The plane normal is not exactly the face normal, so this is not strictly the correct manifold. In particular
			// it will over-limit the rotation for this frame. We could fix this by reverse-projecting the edge points based on the angle...
			if (bIsBoxFaceContact)
			{
				// Initialize the manifold with the nearest plane and nearest point
				Constraint.SetManifoldPlane(1, INDEX_NONE, NormalBoxSpace, BoxClosestBoxSpace);
				Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleClosestBoxSpace));

				// Calculate the capsule edge nearest the face
				FVec3 CapsuleVert0 = CapsuleToBoxTM.TransformPosition(Capsule.GetX1()) + Capsule.GetMargin() * CapsuleRadiusDir;
				FVec3 CapsuleVert1 = CapsuleToBoxTM.TransformPosition(Capsule.GetX2()) + Capsule.GetMargin() * CapsuleRadiusDir;

				// Clip the capsule edge to the axis-aligned box face
				for (int32 ClipAxisIndex = 0; ClipAxisIndex < 3; ++ClipAxisIndex)
				{
					if (ClipAxisIndex != FaceAxisIndex)
					{
						bool bAcceptedPos = Utilities::ClipLineSegmentToAxisAlignedPlane(CapsuleVert0, CapsuleVert1, ClipAxisIndex, 1.0f, Box.Max()[ClipAxisIndex]);
						bool bAcceptedNeg = Utilities::ClipLineSegmentToAxisAlignedPlane(CapsuleVert0, CapsuleVert1, ClipAxisIndex, -1.0f, Box.Min()[ClipAxisIndex]);
						if (!bAcceptedPos || !bAcceptedNeg)
						{
							// Capsule edge is outside a face - stick with the single point we have
							return;
						}
					}
				}

				// Add the clipped points to the manifold if they are not too close to each other, or the near point calculated above.
				// Note: verts are in box space - need to be in capsule space
				// @todo(ccaulfield): manifold point distance tolerance should be a per-solver or per object setting
				const FReal DistanceToleranceSq = (0.1f * Capsule.GetHeight()) * (0.1f * Capsule.GetHeight());
				bool bUserVert0 = ((CapsuleVert1 - CapsuleVert0).SizeSquared() > DistanceToleranceSq) && ((CapsuleVert0 - CapsuleClosestBoxSpace).SizeSquared() > DistanceToleranceSq);
				bool bUseVert1 = ((CapsuleVert1 - CapsuleClosestBoxSpace).SizeSquared() > DistanceToleranceSq);
				if (bUserVert0)
				{ 
					Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleVert0));
				}
				if (bUseVert1)
				{
					Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleVert1));
				}

				return;
			}

			// If the box edge and the capsule edge are the closest features, we treat the capsule as the
			// plane owner. Then select the most-parallel box face as the source of manifold points. The
			// manifold points are found by projecting the capsule verts onto the face and clipping to it.
			// We have a capsule edge if the normal is perpendicular to the capsule axis
			if (bIsBoxEdgeContact && bIsCapsuleEdgeContact)
			{
				// Initialize the manifold with the plane and point on the capsule (move to capsule space)
				FVec3 NormalCapsuleSpace = CapsuleToBoxTM.InverseTransformVector(NormalBoxSpace);
				FVec3 CapsuleClosestCapsuleSpace = CapsuleToBoxTM.InverseTransformPosition(CapsuleClosestBoxSpace);
				Constraint.SetManifoldPlane(0, INDEX_NONE, -NormalCapsuleSpace, CapsuleClosestCapsuleSpace);
				Constraint.AddManifoldPoint(BoxClosestBoxSpace);

				// Project the capsule edge onto the box face
				FReal FaceAxisSign = FMath::Sign(NormalBoxSpace[FaceAxisIndex]);
				FReal FacePos = (FaceAxisSign >= 0.0f)? Box.Max()[FaceAxisIndex] : Box.Min()[FaceAxisIndex];
				FVec3 CapsuleVert0 = CapsuleToBoxTM.TransformPosition(Capsule.GetX1());
				FVec3 CapsuleVert1 = CapsuleToBoxTM.TransformPosition(Capsule.GetX2());
				Utilities::ProjectPointOntoAxisAlignedPlane(CapsuleVert0, CapsuleRadiusDir, FaceAxisIndex, FaceAxisSign, FacePos);
				Utilities::ProjectPointOntoAxisAlignedPlane(CapsuleVert1, CapsuleRadiusDir, FaceAxisIndex, FaceAxisSign, FacePos);

				// Clip the capsule edge to the axis-aligned box face
				for (int32 ClipAxisIndex = 0; ClipAxisIndex < 3; ++ClipAxisIndex)
				{
					if (ClipAxisIndex != FaceAxisIndex)
					{
						bool bAcceptedPos = Utilities::ClipLineSegmentToAxisAlignedPlane(CapsuleVert0, CapsuleVert1, ClipAxisIndex, 1.0f, Box.Max()[ClipAxisIndex]);
						bool bAcceptedNeg = Utilities::ClipLineSegmentToAxisAlignedPlane(CapsuleVert0, CapsuleVert1, ClipAxisIndex, -1.0f, Box.Min()[ClipAxisIndex]);
						if (!bAcceptedPos || !bAcceptedNeg)
						{
							// Capsule edge is outside a face - stick with the single point we have
							return;
						}
					}
				}

				// Add the clipped points to the manifold if they are not too close to each other, or the near point calculated above.
				// Note: verts are in box space - need to be in capsule space
				// @todo(ccaulfield): manifold point distance tolerance should be a per-solver or per object setting
				const FReal DistanceToleranceSq = (0.1f * Capsule.GetHeight()) * (0.1f * Capsule.GetHeight());
				bool bUserVert0 = ((CapsuleVert1 - CapsuleVert0).SizeSquared() > DistanceToleranceSq) && ((CapsuleVert0 - BoxClosestBoxSpace).SizeSquared() > DistanceToleranceSq);
				bool bUseVert1 = ((CapsuleVert1 - BoxClosestBoxSpace).SizeSquared() > DistanceToleranceSq);
				if (bUserVert0)
				{
					Constraint.AddManifoldPoint(CapsuleVert0);
				}
				if (bUseVert1)
				{
					Constraint.AddManifoldPoint(CapsuleVert1);
				}

				return;
			}

			// All feature combinations should be covered above
			ensure(false);
		}

		template<typename T, int d>
		void ConstructCapsuleBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const TBox<T, d> * Object1 = Implicit1->template GetObject<const TBox<T, d> >();
			if (ensure(Object0 && Object1))
			{
				// Box-space AABB check
				// @todo(chaos): avoid recalc of relative transform in GJK (will have to switch order of box/capsule)
				const FRigidTransform3 CapsuleToBoxTM = Transform0.GetRelativeTransform(Transform1);
				const FVec3 P1 = CapsuleToBoxTM.TransformPosition(Object0->GetX1());
				const FVec3 P2 = CapsuleToBoxTM.TransformPosition(Object0->GetX2());
				TAABB<T, 3> CapsuleAABB(P1.ComponentMin(P2), P1.ComponentMax(P2));
				CapsuleAABB.Thicken(Object0->GetRadius() + CullDistance);
				if (CapsuleAABB.Intersects(Object1->GetAABB()))
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));

					if (Chaos_Collision_UseManifolds)
					{ 
						FRigidBodyMultiPointContactConstraint Constraint = FRigidBodyMultiPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleBox);
						UpdateCapsuleBoxManifold(*Object0, Transform0, Object1->BoundingBox(), Transform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleBox);
						UpdateCapsuleBoxConstraint(*Object0, Transform0, Object1->BoundingBox(), Transform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
				}
			}
		}

		//
		// Capsule-HeightField
		//

		template <typename T, int d>
		TContactPoint<T> CapsuleHeightFieldContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TCapsule<float> >(A, ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d>
		void UpdateCapsuleHeightFieldConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleHeightFieldContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		template<class T, int d>
		void UpdateCapsuleHeightFieldManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructCapsuleHeightFieldConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateCapsuleHeightFieldConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Capsule-TriangleMesh
		//

		template <typename T, int d>
		TContactPoint<T> CapsuleTriangleMeshContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const FTriangleMeshImplicitObject& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TCapsule<T> >(A, ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d>
		void UpdateCapsuleTriangleMeshConstraint(const TCapsule<T>& Capsule0, const TRigidTransform<T, d>& Transform0, const FTriangleMeshImplicitObject& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleTriangleMeshContactPoint(Capsule0, Transform0, TriangleMesh1, Transform1, CullDistance));
		}


		template <typename T, int d>
		void UpdateCapsuleTriangleMeshManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{

		}

		template<typename T, int d>
		void ConstructCapsuleTriangleMeshConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const FTriangleMeshImplicitObject * Object1 = GetInnerObject<FTriangleMeshImplicitObject>(*Implicit1);
			if (ensure(Object0 && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateCapsuleTriangleMeshConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}


		//
		// Convex - Convex
		//

		template<class T, int d>
		void UpdateConvexConvexConstraint(const FImplicitObject& Implicit0, const TRigidTransform<T, d>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TCollisionConstraintBase<T, d>& ConstraintBase)
		{
			TContactPoint<T> ContactPoint;

			if (ConstraintBase.GetType() == FRigidBodyPointContactConstraint::StaticType())
			{
				ContactPoint = ConvexConvexContactPoint(Implicit0, Transform0, Implicit1, Transform1, CullDistance);
			}
			else if (ConstraintBase.GetType() == FRigidBodyMultiPointContactConstraint::StaticType())
			{
				TRigidBodyMultiPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyMultiPointContactConstraint<T, d>>();
				ContactPoint.Phi = FLT_MAX;

				const TRigidTransform<T, d> AToBTM = Transform0.GetRelativeTransform(Transform1);

				TPlane<T, d> CollisionPlane(Constraint.GetManifoldPlanePosition(), Constraint.GetManifoldPlaneNormal());

				// re-sample the constraint based on the distance from the collision plane.
				for (int32 Idx = 0; Idx < Constraint.NumManifoldPoints(); Idx++)
				{
					FVec3 Location = Constraint.GetManifoldPoint(Idx);
					FVec3 Normal;
					FReal Phi = CollisionPlane.PhiWithNormal(AToBTM.TransformPosition(Location), Normal);

					// save the best point for collision processing	
					if (ContactPoint.Phi > Phi)
					{
						ContactPoint.Phi = Phi;
						ContactPoint.Normal = Transform1.TransformVector(Constraint.GetManifoldPlaneNormal());
						ContactPoint.Location = Transform0.TransformPosition(Location);
					}
				}
			}

			UpdateContactPoint(ConstraintBase.Manifold, ContactPoint);
		}

		template<class T, int d>
		void UpdateConvexConvexManifold(TCollisionConstraintBase<T, d>&  ConstraintBase, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance)
		{
			if (TRigidBodyMultiPointContactConstraint<T,d>* Constraint = ConstraintBase.template As<TRigidBodyMultiPointContactConstraint<T, d>>())
			{
				if (GetInnerType(ConstraintBase.Manifold.Implicit[0]->GetType()) == ImplicitObjectType::Convex)
				{
					UpdateSingleShotManifold(*Constraint, Transform0, Transform1, CullDistance);
				}
				else
				{
					UpdateIterativeManifold(*Constraint, Transform0, Transform1, CullDistance);
				}
			}
		}


		template<class T, int d>
		void ConstructConvexConvexConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
			TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
			FRigidBodyMultiPointContactConstraint Constraint = FRigidBodyMultiPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
			UpdateConvexConvexManifold(Constraint, Transform0, Transform1, CullDistance);
			UpdateConvexConvexConstraint(*Implicit0, Transform0, *Implicit1, Transform1, CullDistance, Constraint);
			NewConstraints.TryAdd(CullDistance, Constraint);
		}

		//
		// Convex - HeightField
		//

		template <typename T, int d>
		TContactPoint<T> ConvexHeightFieldContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< FConvex >(A, ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d>
		void UpdateConvexHeightFieldConstraint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, ConvexHeightFieldContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		template<class T, int d>
		void UpdateConvexHeightFieldManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T, int d>
		void ConstructConvexHeightFieldConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Implicit0->IsConvex() && Object1))
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateConvexHeightFieldConstraint(*Implicit0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Convex-TriangleMesh
		//

		template <typename T, int d>
		TContactPoint<T> ConvexTriangleMeshContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const FTriangleMeshImplicitObject& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< FConvex >(A, ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d>
		void UpdateConvexTriangleMeshConstraint(const FImplicitObject& Convex0, const TRigidTransform<T, d>& Transform0, const FTriangleMeshImplicitObject& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, ConvexTriangleMeshContactPoint(Convex0, Transform0, TriangleMesh1, Transform1, CullDistance));
		}


		template <typename T, int d>
		void UpdateConvexTriangleMeshManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{

		}

		template<typename T, int d>
		void ConstructConvexTriangleMeshConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const FTriangleMeshImplicitObject * Object1 = GetInnerObject<FTriangleMeshImplicitObject>(*Implicit1);
			if (ensure(Implicit0->IsConvex() && Object1) )
			{
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);
				UpdateConvexTriangleMeshConstraint(*Implicit0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}




		//
		// Levelset-Levelset
		//

		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetLevelsetConstraint"), STAT_UpdateLevelsetLevelsetConstraint, STATGROUP_ChaosWide);
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateLevelsetLevelsetConstraint(const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
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
				SampleObject<UpdateType>(*Particle1->Geometry(), LevelsetTM, *SampleParticles, ParticlesTM, CullDistance, Constraint);
			}
		}

		template<class T, int d>
		void UpdateLevelsetLevelsetManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}


		template<typename T, int d>
		void ConstructLevelsetLevelsetConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
			TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
			FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM);

			bool bIsParticleDynamic0 = Particle0->CastToRigidParticle() && Particle0->ObjectState() == EObjectStateType::Dynamic;
			if (!Particle1->Geometry() || (bIsParticleDynamic0 && !Particle0->CastToRigidParticle()->CollisionParticlesSize() && Particle0->Geometry() && !Particle0->Geometry()->IsUnderlyingUnion()))
			{
				Constraint.Particle[0] = Particle1;
				Constraint.Particle[1] = Particle0;
				Constraint.SetManifold(Implicit1, Implicit0);
			}
			else
			{
				Constraint.Particle[0] = Particle0;
				Constraint.Particle[1] = Particle1;
				Constraint.SetManifold(Implicit0, Implicit1);
			}

			UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Any>(CullDistance, Constraint);

			NewConstraints.TryAdd(CullDistance, Constraint);
		}

		//
		//  Union-Union
		//

		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::ConstructUnionUnionConstraints"), STAT_ConstructUnionUnionConstraints, STATGROUP_ChaosWide);
		template<typename T, int d>
		void ConstructUnionUnionConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			SCOPE_CYCLE_COUNTER(STAT_ConstructUnionUnionConstraints);

			const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(Implicit0, Transform0, *Implicit1, Transform1, CullDistance);

			for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
			{
				const FImplicitObject* LevelsetInnerObj = LevelsetObjPair.First;
				const TRigidTransform<T, d>& LevelsetInnerObjTM = LevelsetObjPair.Second * Transform1;

				//now find all particle inner objects
				const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes(LevelsetInnerObj, LevelsetInnerObjTM, *Implicit0, Transform0, CullDistance);

				//for each inner obj pair, update constraint
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticlePair : ParticleShapes)
				{
					const FImplicitObject* ParticleInnerObj = ParticlePair.First;
					ConstructConstraints<T, d>(Particle0, Particle1, ParticleInnerObj, LevelsetInnerObj, Transform0, Transform1, CullDistance, NewConstraints);
				}
			}
		}

		//
		// Constraint API
		//

		template<typename T, int d>
		void UpdateManifold(TCollisionConstraintBase<T, d>& ConstraintBase, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			if (ConstraintBase.GetType() != TCollisionConstraintBase<T, d>::FType::MultiPoint)
			{
				return;
			}
			TRigidBodyMultiPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyMultiPointContactConstraint<T, d>>();

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
			//    Modify Construct() and Update() to use a CollisionMap indexed on 
			//    EImplicitObjectType, instead of the if/else chain. Also, remove 
			//    the blocks with the ensure(false), they are just for validation 
			//    after the recent change. 
			//
			// @todo(chaos): use constraint ShapesType to index manifold update function
			//
			EImplicitObjectType Implicit0Type = GetInnerType(Implicit0.GetType());
			EImplicitObjectType Implicit1Type = GetInnerType(Implicit1.GetType()); 
			
			if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateBoxBoxManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateBoxHeightFieldManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				UpdateSphereSphereManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateSphereHeightFieldManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateBoxPlaneManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateSpherePlaneManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateSphereBoxManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateSphereCapsuleManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateCapsuleCapsuleManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateCapsuleBoxManifold<T, d>(*Constraint.Manifold.Implicit[0]->template GetObject<const TCapsule<T>>(), Transform0, Constraint.Manifold.Implicit[1]->template GetObject<const TBox<T, d>>()->GetAABB(), Transform1, CullDistance, Constraint);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateCapsuleHeightFieldManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
#if !UE_BUILD_SHIPPING
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ensure(false);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				//UpdatePlaneBoxManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ensure(false);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//UpdatePlaneSphereManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//UpdateBoxSphereManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				//UpdateBoxCapsuleManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//UpdateCapsuleSphereManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				//UpdateBoxTriangleMeshManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ensure(false);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				//UpdateSphereTriangleMeshManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ensure(false);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				//UpdateCapsuleTriangleMeshManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ensure(false);
			}
			else if (Implicit0Type == FConvex::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				//UpdateConvexTriangleMeshManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == FConvex::StaticType())
			{
				ensure(false);
			}
			//
			// the generic convex bodies are last
			//

			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1.IsConvex())
			{
				ensure(false);
			}
#endif
			else if (Implicit0.IsConvex() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateConvexHeightFieldManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else if (Implicit0.IsConvex() && Implicit1.IsConvex())
			{
				UpdateConvexConvexManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}
			else
			{
				UpdateLevelsetLevelsetManifold(ConstraintBase, Transform0, Transform1, CullDistance);
			}

		}

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateAnyConstraint(TCollisionConstraintBase<T, d>& ConstraintBase, const FImplicitObject& Implicit0, const FImplicitObject& Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance)
		{
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
				UpdateBoxBoxConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateBoxHeightFieldConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				UpdateSphereSphereConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TSphere<T, d>>(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateSphereHeightFieldConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateBoxPlaneConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *Implicit1.template GetObject<TPlane<T, d>>(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				UpdateSpherePlaneConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TPlane<T, d>>(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateSphereBoxConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateSphereCapsuleConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				UpdateCapsuleCapsuleConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				UpdateCapsuleBoxConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateCapsuleHeightFieldConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				TRigidBodyPointContactConstraint<T, d>& Constraint = *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >();
				TRigidBodyPointContactConstraint<T, d> TmpConstraint = Constraint;
				UpdateBoxPlaneConstraint(Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, *Implicit0.template GetObject<TPlane<T, d>>(), Transform0, CullDistance, TmpConstraint);
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
				UpdateSpherePlaneConstraint(*Implicit1.template GetObject<TSphere<T, d>>(), Transform1, *Implicit0.template GetObject<TPlane<T, d>>(), Transform0, CullDistance, TmpConstraint);
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
				UpdateSphereBoxConstraint(*Implicit1.template GetObject<TSphere<T, d>>(), Transform1, Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, CullDistance, TmpConstraint);
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
				UpdateCapsuleBoxConstraint(*Implicit1.template GetObject<TCapsule<T>>(), Transform1, Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, CullDistance, TmpConstraint);
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
				UpdateSphereCapsuleConstraint(*Implicit1.template GetObject<TSphere<T, d>>(), Transform1, *Implicit0.template GetObject<TCapsule<T>>(), Transform0, CullDistance, TmpConstraint);
				if (TmpConstraint.GetPhi() < Constraint.GetPhi())
				{
					Constraint = TmpConstraint;
					Constraint.SetNormal(-Constraint.GetNormal());
				}
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				UpdateBoxTriangleMeshConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *GetInnerObject<FTriangleMeshImplicitObject>(Implicit1), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				UpdateSphereTriangleMeshConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *GetInnerObject<FTriangleMeshImplicitObject>(Implicit1), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				UpdateCapsuleTriangleMeshConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *GetInnerObject<FTriangleMeshImplicitObject>(Implicit1), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
#if !UE_BUILD_SHIPPING
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				//     This case should not be necessary. The height fields 
				//     will only ever be collided against, so ideally will never 
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always 
				//     second.
				ensure(false);
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
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//     This case should not be necessary. The height fields 
				//     will only ever be collided against, so ideally will never 
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always 
				//     second.
				ensure(false);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				//     This case should not be necessary. The triangle mesh
				//     will only ever be collided against, so ideally will never
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always
				//     second.
				ensure(false);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				//     This case should not be necessary. The triangle mesh
				//     will only ever be collided against, so ideally will never
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always
				//     second.
				ensure(false);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TCapsule<T>::StaticType() )
			{
				//     This case should not be necessary. The triangle mesh
				//     will only ever be collided against, so ideally will never
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always
				//     second.
				ensure(false);
			}

			//
			// the generic convex bodies are last
			//


			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1.IsConvex())
			{
				//     This case should not be necessary. The height fields 
				//     will only ever be collided against, so ideally will never 
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always 
				//     second.
				ensure(false);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1.IsConvex())
			{
				//     This case should not be necessary. The triangle mesh
				//     will only ever be collided against, so ideally will never
				//     be in index[0] position of the constraint, also the construction
				//     of the constraint will just switch the index position so its always
				//     second.
				ensure(false);
			}
#endif
			else if (Implicit0.IsConvex() && Implicit1Type == THeightField<T>::StaticType())
			{
				UpdateConvexHeightFieldConstraint(Implicit0, Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0Type == FConvex::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				UpdateConvexTriangleMeshConstraint(Implicit0, Transform0, *GetInnerObject<FTriangleMeshImplicitObject>(Implicit1), Transform1, CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>());
			}
			else if (Implicit0.IsConvex() && Implicit1.IsConvex())
			{
				UpdateConvexConvexConstraint(Implicit0, Transform0, Implicit1, Transform1, CullDistance, ConstraintBase);
			}
			else
			{
				UpdateLevelsetLevelsetConstraint<UpdateType>(CullDistance, *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d> >());
			}
		}

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateConstraint(TCollisionConstraintBase<T, d>& ConstraintBase, const TRigidTransform<T, d>& ParticleTransform0, const TRigidTransform<T, d>& ParticleTransform1, const T CullDistance)
		{
			if (ConstraintBase.GetType() == TCollisionConstraintBase<T, d>::FType::MultiPoint)
			{
				// Select the best point from the available manifold points as the current contact
				TRigidBodyMultiPointContactConstraint<T, d>& MultiPointConstraint = *ConstraintBase.template As<TRigidBodyMultiPointContactConstraint<T, d>>();
				UpdateConstraintFromManifold(MultiPointConstraint, ParticleTransform0, ParticleTransform1, CullDistance);
			}
			else if (ConstraintBase.GetType() == TCollisionConstraintBase<T, d>::FType::SinglePoint)
			{
				const FImplicitObject& Implicit0 = *ConstraintBase.Manifold.Implicit[0];
				const FImplicitObject& Implicit1 = *ConstraintBase.Manifold.Implicit[1];
				const TRigidTransform<T, d> Transform0 = ConstraintBase.ImplicitTransform[0] * ParticleTransform0;
				const TRigidTransform<T, d> Transform1 = ConstraintBase.ImplicitTransform[1] * ParticleTransform1;

				// Re-run collision detection to generate a new contact
				// @todo(chaos): all constraints should have a ShapesType set in ConstructConstraints. Then we can use a function table for all.
				TRigidBodyPointContactConstraint<T, d>& PointConstraint = *ConstraintBase.template As<TRigidBodyPointContactConstraint<T, d>>();
				switch (ConstraintBase.Manifold.ShapesType)
				{
				case EContactShapesType::CapsuleCapsule:
					UpdateCapsuleCapsuleConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, CullDistance, PointConstraint);
					break;
				case EContactShapesType::CapsuleBox:
					UpdateCapsuleBoxConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, CullDistance, PointConstraint);
					break;
				case EContactShapesType::BoxBox:
					UpdateBoxBoxConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, CullDistance, PointConstraint);
					break;
				default:
					UpdateAnyConstraint<UpdateType, T, d>(ConstraintBase, Implicit0, Implicit1, Transform0, Transform1, CullDistance);
					break;
				}
			}
		}

		template<typename T, int d>
		void ConstructConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetType()) : ImplicitObjectType::Unknown;

			// If either shape is disabled for collision bail without constructing a constraint
			if (const TPerShapeData<T, d>* Shape0 = Particle0->GetImplicitShape(Implicit0))
			{
				if (Shape0->bDisable)
				{
					return;
				}
				if (Implicit0Type == ImplicitObjectType::TriangleMesh && Shape0->CollisionTraceType != Chaos_CTF_UseComplexAsSimple)
				{
					return;
				}
				else if (Shape0->CollisionTraceType == Chaos_CTF_UseComplexAsSimple && Implicit0Type != ImplicitObjectType::TriangleMesh)
				{
					return;
				}
			} 
			else if (Implicit0Type == ImplicitObjectType::TriangleMesh) 
			{
				return;
			}

			if (const TPerShapeData<T, d>* Shape1 = Particle1->GetImplicitShape(Implicit1))
			{
				if (Shape1->bDisable)
				{
					return;
				}
				if (Implicit1Type == ImplicitObjectType::TriangleMesh && Shape1->CollisionTraceType != Chaos_CTF_UseComplexAsSimple)
				{
					return;
				}
				else if (Shape1->CollisionTraceType == Chaos_CTF_UseComplexAsSimple && Implicit1Type != ImplicitObjectType::TriangleMesh)
				{
					return;
				}

			}
			else if (Implicit1Type == ImplicitObjectType::TriangleMesh)
			{
				return;
			}


			if (!Implicit0 || !Implicit1)
			{
				ConstructLevelsetLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
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
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), Implicit1, TransformedTransform0, Transform1, CullDistance, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform1 = TransformedImplicit1->GetTransform() * Transform1;
				ConstructConstraints(Particle0, Particle1, Implicit0, TransformedImplicit1->GetTransformedObject(), Transform0, TransformedTransform1, CullDistance, NewConstraints);
				return;
			}

			else if (Implicit0OuterType == TImplicitObjectInstanced<FConvex>::StaticType())
			{
				const TImplicitObjectInstanced<FConvex>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectInstanced<FConvex>>();
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetInstancedObject(), Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectInstanced<FConvex>::StaticType())
			{
				const TImplicitObjectInstanced<FConvex>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectInstanced<FConvex>>();
				ConstructConstraints(Particle0, Particle1, Implicit0, TransformedImplicit1->GetInstancedObject(), Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}


			else if (Implicit0OuterType == TImplicitObjectInstanced<TBox<FReal,3>>::StaticType())
			{
				const TImplicitObjectInstanced<TBox<FReal, 3>>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectInstanced<TBox<FReal, 3>>>();
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetInstancedObject(), Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectInstanced<TBox<FReal, 3>>::StaticType())
			{
				const TImplicitObjectInstanced<TBox<FReal, 3>>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectInstanced<TBox<FReal, 3>>>();
				ConstructConstraints(Particle0, Particle1, Implicit0, TransformedImplicit1->GetInstancedObject(), Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}

			else if (Implicit0OuterType == TImplicitObjectInstanced<TCapsule<FReal>>::StaticType())
			{
				const TImplicitObjectInstanced<TCapsule<FReal>>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectInstanced<TCapsule<FReal>>>();
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetInstancedObject(), Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectInstanced<TCapsule<FReal>>::StaticType())
			{
				const TImplicitObjectInstanced<TCapsule<FReal>>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectInstanced<TCapsule<FReal>>>();
				ConstructConstraints(Particle0, Particle1, Implicit0, TransformedImplicit1->GetInstancedObject(), Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}

			else if (Implicit0OuterType == TImplicitObjectInstanced<TSphere<FReal, 3>>::StaticType())
			{
				const TImplicitObjectInstanced<TSphere<FReal, 3>>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectInstanced<TSphere<FReal, 3>>>();
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetInstancedObject(), Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectInstanced<TSphere<FReal, 3>>::StaticType())
			{
				const TImplicitObjectInstanced<TSphere<FReal, 3>>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectInstanced<TSphere<FReal, 3>>>();
				ConstructConstraints(Particle0, Particle1, Implicit0, TransformedImplicit1->GetInstancedObject(), Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}

			else if (Implicit0OuterType == TImplicitObjectInstanced<FConvex>::StaticType())
			{
				const TImplicitObjectInstanced<FConvex>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectInstanced<FConvex>>();
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetInstancedObject(), Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectInstanced<FConvex>::StaticType())
			{
				const TImplicitObjectInstanced<FConvex>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectInstanced<FConvex>>();
				ConstructConstraints(Particle0, Particle1, Implicit0, TransformedImplicit1->GetInstancedObject(), Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}
			else if (Implicit0OuterType != FImplicitObjectUnion::StaticType() && Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(Implicit0, Transform0, *Implicit1, Transform1, CullDistance);
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
				{
					const FImplicitObject* Implicit1InnerObj = LevelsetObjPair.First;
					const TRigidTransform<T, d> Implicit1InnerObjTM = LevelsetObjPair.Second * Transform1;
					ConstructConstraints(Particle0, Particle1, Implicit0, Implicit1InnerObj, Transform0, Implicit1InnerObjTM, CullDistance, NewConstraints);
				}
				return;
			}
			else if (Implicit0OuterType == FImplicitObjectUnion::StaticType() && Implicit1OuterType != FImplicitObjectUnion::StaticType())
			{
				// [Note] forces non-unions into particle[0] position
				const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes(Implicit1, Transform1, *Implicit0, Transform0, CullDistance);
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
				{
					const FImplicitObject* Implicit0InnerObj = LevelsetObjPair.First;
					const TRigidTransform<T, d> Implicit0InnerObjTM = LevelsetObjPair.Second * Transform0;
					ConstructConstraints(Particle0, Particle1, Implicit0InnerObj, Implicit1, Implicit0InnerObjTM, Transform1, CullDistance, NewConstraints);
				}
				return;
			}
			else if (Implicit0OuterType == FImplicitObjectUnion::StaticType() && Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				ConstructUnionUnionConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}

			if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				ConstructBoxHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				ConstructBoxPlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxPlaneConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereSphereConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				ConstructSphereHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TPlane<T, d>::StaticType())
			{
				ConstructSpherePlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TPlane<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSpherePlaneConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructSphereBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereBoxConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructSphereCapsuleConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereCapsuleConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructCapsuleCapsuleConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructCapsuleBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructCapsuleBoxConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == THeightField<T>::StaticType())
			{
				ConstructCapsuleHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructCapsuleHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<T, d>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructBoxTriangleMeshConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TBox<T, d>::StaticType())
			{
				ConstructBoxTriangleMeshConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<T, d>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructSphereTriangleMeshConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TSphere<T, d>::StaticType())
			{
				ConstructSphereTriangleMeshConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<T>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructCapsuleTriangleMeshConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TCapsule<T>::StaticType())
			{
				ConstructCapsuleTriangleMeshConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}

			//
			// the generic convex bodies are last
			//

			else if (Implicit0->IsConvex() && Implicit1Type == THeightField<T>::StaticType())
			{
				ConstructConvexHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == THeightField<T>::StaticType() && Implicit1->IsConvex())
			{
				ConstructConvexHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0->IsConvex() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructConvexTriangleMeshConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1->IsConvex())
			{
				ConstructConvexTriangleMeshConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0->IsConvex() && Implicit1->IsConvex())
			{
				ConstructConvexConvexConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else
			{
				ConstructLevelsetLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
		}

		template void UpdateBoxBoxConstraint<float, 3>(const TAABB<float, 3>& Box1, const TRigidTransform<float, 3>& Box1Transform, const TAABB<float, 3>& Box2, const TRigidTransform<float, 3>& Box2Transform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateBoxBoxManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructBoxBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateBoxHeightFieldConstraint<float, 3>(const TAABB<float, 3>& A, const TRigidTransform<float, 3>& ATransform, const THeightField<float>& B, const TRigidTransform<float, 3>& BTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateBoxHeightFieldManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructBoxHeightFieldConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateBoxPlaneConstraint<float, 3>(const TAABB<float, 3>& Box, const TRigidTransform<float, 3>& BoxTransform, const TPlane<float, 3>& Plane, const TRigidTransform<float, 3>& PlaneTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateBoxPlaneManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructBoxPlaneConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereSphereConstraint<float, 3>(const TSphere<float, 3>& Sphere1, const TRigidTransform<float, 3>& Sphere1Transform, const TSphere<float, 3>& Sphere2, const TRigidTransform<float, 3>& Sphere2Transform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSphereSphereManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructSphereSphereConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereHeightFieldConstraint<float, 3>(const TSphere<float, 3>& A, const TRigidTransform<float, 3>& ATransform, const THeightField<float>& B, const TRigidTransform<float, 3>& BTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSphereHeightFieldManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructSphereHeightFieldConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSpherePlaneConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TPlane<float, 3>& Plane, const TRigidTransform<float, 3>& PlaneTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSpherePlaneManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructSpherePlaneConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereBoxConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TAABB<float, 3>& Box, const TRigidTransform<float, 3>& BoxTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSphereBoxManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructSphereBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSphereCapsuleConstraint<float, 3>(const TSphere<float, 3>& Sphere, const TRigidTransform<float, 3>& SphereTransform, const TCapsule<float>& Box, const TRigidTransform<float, 3>& BoxTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateSphereCapsuleManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructSphereCapsuleConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateCapsuleCapsuleConstraint<float, 3>(const TCapsule<float>& A, const TRigidTransform<float, 3>& ATransform, const TCapsule<float>& B, const TRigidTransform<float, 3>& BTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateCapsuleCapsuleManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructCapsuleCapsuleConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateCapsuleBoxConstraint<float, 3>(const TCapsule<float>& A, const TRigidTransform<float, 3>& ATransform, const TAABB<float, 3>& B, const TRigidTransform<float, 3>& BTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateCapsuleBoxManifold<float, 3>(const TCapsule<float>& Capsule, const TRigidTransform<float, 3>& CapsuleTM, const TAABB<float, 3>& Box, const TRigidTransform<float, 3>& BoxTM, const float CullDistance, FRigidBodyMultiPointContactConstraint& Constraint);
		template void ConstructCapsuleBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateCapsuleHeightFieldConstraint<float, 3>(const TCapsule<float>& A, const TRigidTransform<float, 3>& ATransform, const THeightField<float>& B, const TRigidTransform<float, 3>& BTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateCapsuleHeightFieldManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructCapsuleHeightFieldConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateConvexConvexConstraint<float, 3>(const FImplicitObject& A, const TRigidTransform<float, 3>& ATM, const FImplicitObject& B, const TRigidTransform<float, 3>& BTM, const float CullDistance, TCollisionConstraintBase<float, 3>& Constraint);
		template void UpdateConvexConvexManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructConvexConvexConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateConvexHeightFieldConstraint<float, 3>(const FImplicitObject& A, const TRigidTransform<float, 3>& ATransform, const THeightField<float>& B, const TRigidTransform<float, 3>& BTransform, const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateConvexHeightFieldManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructConvexHeightFieldConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Any, float, 3>(const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest, float, 3>(const float CullDistance, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void UpdateLevelsetLevelsetManifold<float, 3>(TCollisionConstraintBase<float, 3>&  Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void ConstructLevelsetLevelsetConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void ConstructUnionUnionConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

		template void UpdateSingleShotManifold<float, 3>(TRigidBodyMultiPointContactConstraint<float, 3>&  Constraint, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void UpdateIterativeManifold<float, 3>(TRigidBodyMultiPointContactConstraint<float, 3>&  Constraint, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void UpdateManifold<float, 3>(TCollisionConstraintBase<float, 3>& ConstraintBase, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance);
		template void UpdateConstraint<ECollisionUpdateType::Any, float, 3>(TCollisionConstraintBase<float, 3>& ConstraintBase, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void UpdateConstraint<ECollisionUpdateType::Deepest, float, 3>(TCollisionConstraintBase<float, 3>& ConstraintBase, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void UpdateConstraintFromManifold(TRigidBodyMultiPointContactConstraint<float, 3>& Constraint, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void ConstructConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, FCollisionConstraintsArray& NewConstraints);

	} // Collisions

} // Chaos
