// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolution.h"

#include "Chaos/CastingUtilities.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Capsule.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Collision/CollisionContext.h"
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

#if 0
DECLARE_CYCLE_STAT(TEXT("Collisions::GJK"), STAT_Collisions_GJK, STATGROUP_ChaosCollision);
#define SCOPE_CYCLE_COUNTER_GJK() SCOPE_CYCLE_COUNTER(STAT_Collisions_GJK)
#else
#define SCOPE_CYCLE_COUNTER_GJK()
#endif

//PRAGMA_DISABLE_OPTIMIZATION

float CCDEnableThresholdBoundsScale = 0.4f;
FAutoConsoleVariableRef  CVarCCDEnableThresholdBoundsScale(TEXT("p.Chaos.CCD.EnableThresholdBoundsScale"), CCDEnableThresholdBoundsScale , TEXT("CCD is used when object position is changing > smallest bound's extent * BoundsScale. 0 will always Use CCD. Values < 0 disables CCD."));

float CCDAllowedDepthBoundsScale = 0.05f;
FAutoConsoleVariableRef CVarCCDAllowedDepthBoundsScale(TEXT("p.Chaos.CCD.AllowedDepthBoundsScale"), CCDAllowedDepthBoundsScale, TEXT("When rolling back to TOI, allow (smallest bound's extent) * AllowedDepthBoundsScale, instead of rolling back to exact TOI w/ penetration = 0."));

// If GJKPenetration returns a phi of abs value < this number, we use PhiWithNormal to resample phi and normal.
// We have observed bad normals coming from GJKPenetration when barely in contact.
#define PHI_RESAMPLE_THRESHOLD 0.001


bool Chaos_Collision_UseManifolds = true;
FAutoConsoleVariableRef CVarChaosCollisionUseManifolds(TEXT("p.Chaos.Collision.UseManifolds"), Chaos_Collision_UseManifolds, TEXT("Enable/Disable use of manifoldes in collision."));

bool Chaos_Collision_UseManifolds_Test = false;
FAutoConsoleVariableRef CVarChaosCollisionUseManifoldsTest(TEXT("p.Chaos.Collision.UseManifoldsTest"), Chaos_Collision_UseManifolds_Test, TEXT("Enable/Disable use of manifoldes in collision."));

float Chaos_Collision_ManifoldFaceAngle = 5.0f;
float Chaos_Collision_ManifoldFaceEpsilon = FMath::Sin(FMath::DegreesToRadians(Chaos_Collision_ManifoldFaceAngle));
FConsoleVariableDelegate Chaos_Collision_ManifoldFaceDelegate = FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* CVar) { Chaos_Collision_ManifoldFaceEpsilon = FMath::Sin(FMath::DegreesToRadians(Chaos_Collision_ManifoldFaceAngle)); });
FAutoConsoleVariableRef CVarChaosCollisionManifoldFaceAngle(TEXT("p.Chaos.Collision.ManifoldFaceAngle"), Chaos_Collision_ManifoldFaceAngle, TEXT("Angle above which a face is rejected and we switch to point collision"), Chaos_Collision_ManifoldFaceDelegate);

float Chaos_Collision_CapsuleBoxManifoldAngle = 20.0f;
float Chaos_Collision_CapsuleBoxManifoldTolerance = FMath::Sin(FMath::DegreesToRadians(Chaos_Collision_CapsuleBoxManifoldAngle));
FConsoleVariableDelegate Chaos_Collision_CapsuleBoxManifoldDelegate = FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* CVar) { Chaos_Collision_CapsuleBoxManifoldTolerance = FMath::Sin(FMath::DegreesToRadians(Chaos_Collision_CapsuleBoxManifoldAngle)); });
FAutoConsoleVariableRef CVarChaosCollisionBoxCapsuleManifoldAngle(TEXT("p.Chaos.Collision.CapsuleBoxManifoldAngle"), Chaos_Collision_CapsuleBoxManifoldAngle, TEXT("If a capsule is more than this angle from vertical, do not use a manifold"), Chaos_Collision_CapsuleBoxManifoldDelegate);

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

		// Determines if body should use CCD. If using CCD, computes Dir and Length of sweep.
		template <typename T, int d>
		bool UseCCD(const TGeometryParticleHandle<T, d>* SweptParticle, const TGeometryParticleHandle<T,d>* OtherParticle, const FImplicitObject* Implicit, TVector<T,d>& Dir, FReal& Length)
		{
			if (OtherParticle->ObjectState() != EObjectStateType::Static)
			{
				return false;
			}

			auto* RigidParticle= SweptParticle->CastToRigidParticle();
			if (RigidParticle)
			{
				Dir = RigidParticle->P() - RigidParticle->X();
				Length = Dir.Size();

				if (!Implicit->HasBoundingBox())
				{
					return false;
				}

				T MinBoundsAxis = Implicit->BoundingBox().Extents().Min();
				T LengthCCDThreshold = MinBoundsAxis * CCDEnableThresholdBoundsScale;

				if (Length > 0.0f && CCDEnableThresholdBoundsScale >= 0.0f && Length >= LengthCCDThreshold)
				{
					Dir /= Length;
					return true;
				}
			}

			return false;
		}

		void ComputeSweptContactPhiAndTOIHelper(const FVec3& ContactNormal, const FVec3& Dir, const FReal& Length, const FReal& HitTime, FReal& OutTOI, FReal& OutPhi)
		{
			if (HitTime >= 0.0f)
			{
				// We subtract length to get the total penetration at at end of frame.
				// Project penetration vector onto geometry normal for correct phi.
				FReal Dot = FMath::Abs(FVec3::DotProduct(ContactNormal, -Dir));
				OutPhi = (HitTime - Length) * Dot; 
				
				// TOI is between [0,1], used to compute particle position
				OutTOI = HitTime / Length;
			}
			else
			{
				// Initial overlap case:
				// TOI = 0 as we are overlapping at X.
				// OutTime is penetration value of MTD.
				OutPhi = HitTime;
				OutTOI = 0.0f;
			}
		}

		// Initializes TimeOfImpact and adjusts Phi for TOI.
		template <typename T, int d>
		void SetSweptConstraintTOI(TGeometryParticleHandle<T, d>* Particle, const FReal TOI, const FReal Length, const FVec3& Dir, FRigidBodySweptPointContactConstraint& SweptConstraint)
		{
			if (SweptConstraint.Manifold.Phi > 0.0f)
			{
				return;
			}

			TPBDRigidParticleHandle<T, d>* RigidParticle = Particle->CastToRigidParticle();
			if (!CHAOS_ENSURE(RigidParticle))
			{
				return;
			}

			const FReal MinBounds = Particle->Geometry()->BoundingBox().Extents().Min();
			const FReal Depth = -SweptConstraint.Manifold.Phi;
			const FReal MaxDepth = MinBounds * CCDAllowedDepthBoundsScale;

			if (TOI <= 0.0f)
			{
				// Initial overlap. Phi is correct already.
				SweptConstraint.TimeOfImpact = 0.0f;
			}
			else if (TOI > 0.0f && TOI <= 1.0f)
			{
				if (Depth <= MaxDepth)
				{
					// Although we may have an earlier TOI, depth at TOI = 1 is within tolerance, so just use TOI = 1.
					SweptConstraint.TimeOfImpact = 1.0f;
				}
				else if (Depth > MaxDepth)
				{
					// Compute ExtraT that will take us from TOI and 0 phi to slightly past TOI w/ phi at maximum allowed depth.
					const FReal DirDotNormal = FMath::Abs(FVec3::DotProduct(Dir, SweptConstraint.Manifold.Normal));
					const FReal ExtraT = MaxDepth / (DirDotNormal * Length);

					SweptConstraint.TimeOfImpact = TOI + ExtraT;
					SweptConstraint.Manifold.Phi = -MaxDepth;
				}
			}
		}


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
		TContactPoint<T> GJKContactPoint(const GeometryA& A, const TRigidTransform<T, d>& ATM, const GeometryB& B, const TRigidTransform<T, d>& BTM, const TVector<T, 3>& InitialDir)
		{
			SCOPE_CYCLE_COUNTER_GJK();

			TContactPoint<T> Contact;
			const TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);

			T Penetration;
			TVec3<T> ClosestA, ClosestB, Normal;
			int32 NumIterations = 0;

			if (ensure(GJKPenetration<true>(A, B, BToATM, Penetration, ClosestA, ClosestB, Normal, (T)0, InitialDir, (T)0, &NumIterations)))
			{
				Contact.Location = ATM.TransformPosition(ClosestA);
				Contact.Normal = -ATM.TransformVectorNoScale(Normal);
				Contact.Phi = -Penetration;
			}

			//static float AverageIterations = 0;
			//AverageIterations = AverageIterations + ((float)NumIterations - AverageIterations) / 1000.0f;
			//UE_LOG(LogChaos, Warning, TEXT("GJK Its: %f"), AverageIterations);

			return Contact;
		}

		template <typename T, int d, typename GeometryA, typename GeometryB>
		TContactPoint<T> GJKContactPointSwept(const GeometryA& A, const TRigidTransform<T, d>& ATM, const GeometryB& B, const TRigidTransform<T, d>& BTM, const TVector<T, d>& Dir, const T Length, const T CullDistance, T& TOI)
		{
			TContactPoint<T> Contact;
			const TRigidTransform<T, d> AToBTM = ATM.GetRelativeTransform(BTM);
			const TVector<T, d> LocalDir = BTM.InverseTransformVectorNoScale(Dir);

			T OutTime;
			TVec3<T> Location, Normal;
			int32 NumIterations = 0;

			if (GJKRaycast2(B, A, AToBTM, LocalDir, Length, OutTime, Location, Normal, (T)0, true))
			{
				Contact.Location = BTM.TransformPosition(Location);
				Contact.Normal = BTM.TransformVectorNoScale(Normal);
				ComputeSweptContactPhiAndTOIHelper(Contact.Normal, Dir, Length, OutTime, TOI, Contact.Phi);
			}

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
					Contact.Normal = BTransform.TransformVectorNoScale(Normal);
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
						Contact.Normal = BTransform.TransformVectorNoScale(Normal);
					}
				}
			}
			else if (const GeometryA* ConvexImplicit = A.template GetObject<const GeometryA>())
			{
				if (B.GJKContactPoint(*ConvexImplicit, AToBTM, CullDistance, Location, Normal, Penetration))
				{
					Contact.Phi = Penetration;
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVectorNoScale(Normal);
				}
			}

			return Contact;
		}

		template <typename GeometryA, typename GeometryB, typename T, int d>
		TContactPoint<T> GJKImplicitSweptContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& AStartTransform, const GeometryB& B, const TRigidTransform<T, d>& BTransform, const TVector<T, d>& Dir, const T Length, const T CullDistance, T& TOI)
		{
			TContactPoint<T> Contact;
			const TRigidTransform<T, d> AToBTM = AStartTransform.GetRelativeTransform(BTransform);
			const TVector<T, d> LocalDir = BTransform.InverseTransformVectorNoScale(Dir);

			T OutTime = FLT_MAX;
			int32 FaceIndex = -1;
			TVec3<T> Location, Normal;

			Utilities::CastHelper(A, AStartTransform, [&](const auto& ADowncast, const TRigidTransform<T,d>& AFullTM)
			{
				if (B.SweepGeom(ADowncast, AToBTM, LocalDir, Length, OutTime, Location, Normal, FaceIndex, 0.0f, true))
				{
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVectorNoScale(Normal);
					ComputeSweptContactPhiAndTOIHelper(Contact.Normal, Dir, Length, OutTime, TOI, Contact.Phi);
				}
			});

			return Contact;
		}

		// A is the implicit here, we want to return a contact point on B (trimesh)
		template <typename GeometryA, typename T, int d>
		TContactPoint<T> GJKImplicitScaledTriMeshSweptContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& AStartTransform, const TImplicitObjectScaled<FTriangleMeshImplicitObject>& B, const TRigidTransform<T, d>& BTransform, const TVector<T,d>& Dir, const T Length, const T CullDistance, T& TOI)
		{
			TContactPoint<T> Contact;
			const TRigidTransform<T, d> AToBTM = AStartTransform.GetRelativeTransform(BTransform);
			const TVector<T, d> LocalDir = BTransform.InverseTransformVectorNoScale(Dir);

			if (!ensure(B.GetType() & ImplicitObjectType::TriangleMesh) || !ensure(!IsInstanced(B.GetType())))
			{
				return TContactPoint<T>();
			}

			T OutTime = FLT_MAX;
			TVec3<T> Location, Normal;
			int32 FaceIndex = -1;

			Utilities::CastHelper(A, AStartTransform, [&](const auto& ADowncast, const TRigidTransform<T,d>& AFullTM)
			{
				if(B.LowLevelSweepGeom(ADowncast, AToBTM, LocalDir, Length, OutTime, Location, Normal, FaceIndex, 0.0f, true))
				{
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVectorNoScale(Normal);
					ComputeSweptContactPhiAndTOIHelper(Contact.Normal, Dir, Length, OutTime, TOI, Contact.Phi);
				}
			});

			return Contact;
		}



		// This is pretty unnecessary - all instanced shapes have the same implementation so we should be able to
		// collapse this switch into a generic call. Maybe add a base class to TImplicitObjectInstanced.
		inline const FImplicitObject* GetInstancedImplicit(const FImplicitObject* Implicit0)
		{
			EImplicitObjectType Implicit0OuterType = Implicit0->GetType();

			if (Implicit0OuterType == TImplicitObjectInstanced<FConvex>::StaticType())
			{
				return Implicit0->template GetObject<const TImplicitObjectInstanced<FConvex>>()->GetInstancedObject();
			}
			else if (Implicit0OuterType == TImplicitObjectInstanced<TBox<FReal, 3>>::StaticType())
			{
				return Implicit0->template GetObject<const TImplicitObjectInstanced<TBox<FReal, 3>>>()->GetInstancedObject();
			}
			else if (Implicit0OuterType == TImplicitObjectInstanced<TCapsule<FReal>>::StaticType())
			{
				return Implicit0->template GetObject<const TImplicitObjectInstanced<TCapsule<FReal>>>()->GetInstancedObject();
			}
			else if (Implicit0OuterType == TImplicitObjectInstanced<TSphere<FReal, 3>>::StaticType())
			{
				return Implicit0->template GetObject<const TImplicitObjectInstanced<TSphere<FReal, 3>>>()->GetInstancedObject();
			}
			else if (Implicit0OuterType == TImplicitObjectInstanced<FConvex>::StaticType())
			{
				return Implicit0->template GetObject<const TImplicitObjectInstanced<FConvex>>()->GetInstancedObject();
			}
			else if (Implicit0OuterType == TImplicitObjectInstanced<FTriangleMeshImplicitObject>::StaticType())
			{
				return Implicit0->template GetObject<const TImplicitObjectInstanced<FTriangleMeshImplicitObject>>()->GetInstancedObject();
			}

			return nullptr;
		}


		template<class T, int d>
		TContactPoint<T> ConvexConvexContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATM, const FImplicitObject& B, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{
			TContactPoint<T> ContactPoint = Utilities::CastHelper(A, ATM, [&](const auto& ADowncast, const TRigidTransform<T,d>& AFullTM)
			{
				return Utilities::CastHelper(B, BTM, [&](const auto& BDowncast, const TRigidTransform<T,d>& BFullTM)
				{
					return GJKContactPoint(ADowncast, AFullTM, BDowncast, BFullTM, TVector<T, d>(1, 0, 0));
				});
			});

			if (FMath::Abs(ContactPoint.Phi) < (T)(PHI_RESAMPLE_THRESHOLD))
			{
				// If GJKPenetration returns a phi of abs value < this number, we use PhiWithNormal to resample phi and normal.
				// We have observed bad normals coming from GJKPenetration when barely in contact.

				TVector<T, d> ContactLocalB = BTM.InverseTransformPosition(ContactPoint.Location);
				ContactPoint.Phi = B.PhiWithNormal(ContactLocalB, ContactPoint.Normal);
				ContactPoint.Normal = BTM.TransformVectorNoScale(ContactPoint.Normal);
			}

			return ContactPoint;
		}

		template<class T, int d>
		TContactPoint<T> ConvexConvexContactPointSwept(const FImplicitObject& A, const TRigidTransform<T, d>& ATM, const FImplicitObject& B, const TRigidTransform<T, d>& BTM, const TVector<T, d>& Dir, const T Length, const T CullDistance, T& TOI)
		{
			return Utilities::CastHelper(A, ATM, [&](const auto& ADowncast, const TRigidTransform<T,d>& AFullTM)
			{
				return Utilities::CastHelper(B, BTM, [&](const auto& BDowncast, const TRigidTransform<T,d>& BFullTM)
				{
					return GJKContactPointSwept(ADowncast, AFullTM, BDowncast, BFullTM, Dir, Length, CullDistance, TOI);
				});
			});
		}

		template <typename T, int d>
		void UpdateSingleShotManifold(TRigidBodyMultiPointContactConstraint<T, d>& Constraint, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance)
		{
			// single shot manifolds for TConvex implicit object in the constraints implicit[0] position. 
			TContactPoint<T> ContactPoint = ConvexConvexContactPoint(*Constraint.Manifold.Implicit[0], Transform0, *Constraint.Manifold.Implicit[1], Transform1, CullDistance);

			// Cache the nearest point as the initial contact
			Constraint.Manifold.Phi = ContactPoint.Phi;
			Constraint.Manifold.Normal = ContactPoint.Normal;
			Constraint.Manifold.Location = ContactPoint.Location;

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
				const FVec3 PlaneNormal = Transform1.InverseTransformVectorNoScale(ContactPoint.Normal);
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

			// Cache the nearest point as the initial contact
			Constraint.Manifold.Phi = ContactPoint.Phi;
			Constraint.Manifold.Normal = ContactPoint.Normal;
			Constraint.Manifold.Location = ContactPoint.Location;

			if (!ContactPoint.Normal.Equals(Constraint.GetManifoldPlaneNormal()) || !Constraint.NumManifoldPoints())
			{
				Constraint.ResetManifoldPoints();
				FVec3 PlaneNormal = Transform1.InverseTransformVectorNoScale(ContactPoint.Normal);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::BoxBox);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::BoxHeightField);
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
					Contact.Normal = PlaneTransform.TransformVectorNoScale(Normal);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::BoxPlane);
				UpdateBoxPlaneConstraint(Object0->BoundingBox(), Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Box-TriangleMesh
		//

		template <typename TriMeshType, typename T, int d>
		TContactPoint<T> BoxTriangleMeshContactPoint(const TAABB<T, d>& A, const TRigidTransform<T, d>& ATransform, const TriMeshType& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TBox<float, 3> >(TBox<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}

		template <typename TriMeshType, typename T, int d>
		void UpdateBoxTriangleMeshConstraint(const TAABB<T, d>& Box0, const TRigidTransform<T, d>& Transform0, const TriMeshType& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
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
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::BoxTriMesh);
					UpdateBoxTriangleMeshConstraint(Object0->GetAABB(), Transform0, *ScaledTriangleMesh, Transform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else if(const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::BoxTriMesh);
					UpdateBoxTriangleMeshConstraint(Object0->GetAABB(), Transform0, *TriangleMesh, Transform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					ensure(false);
				}
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::SphereSphere);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::SphereHeightField);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::SpherePlane);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::SphereBox);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::SphereCapsule);
				UpdateSphereCapsuleConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Sphere-TriangleMesh
		//

		template <typename T, int d, typename TriMeshType>
		TContactPoint<T> SphereTriangleMeshContactPoint(const TSphere<T, d>& A, const TRigidTransform<T, d>& ATransform, const TriMeshType& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TSphere<float, 3> >(TSphere<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}

		template <typename T, int d, typename TriMeshType>
		void UpdateSphereTriangleMeshConstraint(const TSphere<T, d>& Sphere0, const TRigidTransform<T, d>& Transform0, const TriMeshType& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
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
			const TSphere<T, d>* Object0 = Implicit0->template GetObject<const TSphere<T, d> >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::SphereTriMesh);
					UpdateSphereTriangleMeshConstraint(*Object0, Transform0, *ScaledTriangleMesh, Transform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::SphereTriMesh);
					UpdateSphereTriangleMeshConstraint(*Object0, Transform0, *TriangleMesh, Transform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					ensure(false);
				}
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleCapsule);
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
		void UpdateCapsuleBoxManifold(const TCapsule<T>& Capsule, const TRigidTransform<T, d>& CapsuleTM, const TAABB<T, d>& Box, const TRigidTransform<T, d>& BoxTM, const T CullDistance, const FCollisionContext& Context, FRigidBodyMultiPointContactConstraint& Constraint)
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
				SCOPE_CYCLE_COUNTER_GJK();
				if (!ensure(GJKPenetration<true>(Box, Capsule, CapsuleToBoxTM, Penetration, BoxClosestBoxSpace, CapsuleClosestBoxSpace, NormalBoxSpace, (T)0, InitialDir, (T)0)))
				{
					return;
				}
			}

			// Cache the closest point so we don't need to re-iterate over the manifold on the first iteration.
			Constraint.Manifold.Location = BoxTM.TransformPosition(BoxClosestBoxSpace);
			Constraint.Manifold.Normal = BoxTM.TransformVectorNoScale(NormalBoxSpace);
			Constraint.Manifold.Phi = -Penetration;

			// Find the box feature that the near point is on
			// Face, Edge, or Vertex can be determined from number of non-zero elements in the box-space normal.
			const FReal ComponentEpsilon = Chaos_Collision_ManifoldFaceEpsilon;
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

			FVec3 CapsuleAxis = CapsuleToBoxTM.TransformVectorNoScale(Capsule.GetAxis()); // Box space capsule axis
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
				FVec3 NormalCapsuleSpace = CapsuleToBoxTM.InverseTransformVectorNoScale(NormalBoxSpace);
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
		void ConstructCapsuleBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
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

					bool bAllowManifold = Chaos_Collision_UseManifolds;

					if (bAllowManifold && (Chaos_Collision_CapsuleBoxManifoldTolerance > KINDA_SMALL_NUMBER))
					{
						// @todo(ccaulfield): weak sauce - fix capsule-box manifolds.
						// HACK: Disable manifolds for "horizontal" capsules. Manifolds don't work well when joints are pulling boxes down
						// (under gravity) when the upper boxes are draped over a horizontal capsule. The box rotations about the manifold
						// points(line) is too great and we end up with jitter.
						const FVector CapsuleAxis = Context.SpaceTransform.TransformVectorNoScale(Transform0.TransformVectorNoScale(Object0->GetAxis()));
						bAllowManifold = (FMath::Abs(CapsuleAxis.Z) > Chaos_Collision_CapsuleBoxManifoldTolerance);
					}

					if (bAllowManifold)
					{
						FRigidBodyMultiPointContactConstraint Constraint = FRigidBodyMultiPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleBox);
						UpdateCapsuleBoxManifold(*Object0, Transform0, Object1->BoundingBox(), Transform1, CullDistance, Context, Constraint);
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

		template <typename T, int d>
		void UpdateCapsuleHeightFieldConstraintSwept(TGeometryParticleHandle<T, d>* Particle0, const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const TVector<T, d>& Dir, const T Length, const T CullDistance, TRigidBodySweptPointContactConstraint<T, d>& Constraint)
		{
			T TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, GJKImplicitSweptContactPoint<TCapsule<float> >(A, ATransform, B, BTransform, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleHeightField);
				UpdateCapsuleHeightFieldConstraint(*Object0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		template<typename T, int d>
		void ConstructCapsuleHeightFieldConstraintsSwept(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, const TVector<T,d>& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Object0 && Object1))
			{
				const TRigidTransform<T, 3> TransformX0(Particle0->X(), Particle0->R());
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));

				FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleHeightField);
				UpdateCapsuleHeightFieldConstraintSwept(Particle0, *Object0, TransformX0, *Object1, Transform1, Dir, Length, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}


		//
		// Capsule-TriangleMesh
		//

		template <typename TriMeshType, typename T, int d>
		TContactPoint<T> CapsuleTriangleMeshContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TriMeshType& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< TCapsule<T> >(A, ATransform, B, BTransform, CullDistance);
		}

		template <typename TriMeshType, typename T, int d>
		TContactPoint<T> CapsuleTriangleMeshSweptContactPoint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TriMeshType& B, const TRigidTransform<T, d>& BStartTransform, const TVector<T, d>& Dir, const T Length, const T CullDistance, T& TOI)
		{
			if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = B.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
			{
				return GJKImplicitScaledTriMeshSweptContactPoint<TCapsule<T>>(A, ATransform, *ScaledTriangleMesh, BStartTransform, Dir, Length, CullDistance, TOI);
			}
			else if (const FTriangleMeshImplicitObject* TriangleMesh = B.template GetObject<const FTriangleMeshImplicitObject>())
			{
				return GJKImplicitSweptContactPoint<TCapsule<T>>(A, ATransform, *TriangleMesh, BStartTransform, Dir, Length, CullDistance, TOI);
			}

			ensure(false);
			return TContactPoint<T>();
		}


		template <typename TriMeshType, typename T, int d>
		void UpdateCapsuleTriangleMeshConstraint(const TCapsule<T>& Capsule0, const TRigidTransform<T, d>& Transform0, const TriMeshType& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleTriangleMeshContactPoint(Capsule0, Transform0, TriangleMesh1, Transform1, CullDistance));
		}

		template <typename TriMeshType, typename T, int d>
		void UpdateCapsuleTriangleMeshConstraintSwept(TGeometryParticleHandle<T, d>* Particle0, const TCapsule<T>& Capsule0, const TRigidTransform<T, d>& Transform0, const TriMeshType& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const TVector<T, d>& Dir, const T Length, const T CullDistance, TRigidBodySweptPointContactConstraint<T, d>& Constraint)
		{
			T TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, CapsuleTriangleMeshSweptContactPoint(Capsule0, Transform0, TriangleMesh1, Transform1, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template <typename T, int d>
		void UpdateCapsuleTriangleMeshManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{

		}

		template<typename T, int d>
		void ConstructCapsuleTriangleMeshConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleTriMesh);
					UpdateCapsuleTriangleMeshConstraint(*Object0, Transform0, *ScaledTriangleMesh, Transform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleTriMesh);
					UpdateCapsuleTriangleMeshConstraint(*Object0, Transform0, *TriangleMesh, Transform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					ensure(false);
				}
			}
		}

		template<typename T, int d>
		void ConstructCapsuleTriangleMeshConstraintsSwept(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, const TVector<T,d>& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<T> * Object0 = Implicit0->template GetObject<const TCapsule<T> >();
			if (ensure(Object0))
			{
				TRigidTransform<T, 3> TransformX0(Particle0->X(), Particle0->R());
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));

				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleTriMesh);
					UpdateCapsuleTriangleMeshConstraintSwept(Particle0, *Object0, TransformX0, *ScaledTriangleMesh, Transform1, Dir, Length, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::CapsuleTriMesh);
					UpdateCapsuleTriangleMeshConstraintSwept(Particle0, *Object0, TransformX0, *TriangleMesh, Transform1, Dir, Length, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					ensure(false);
				}
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
			else if (ConstraintBase.GetType() == FRigidBodySweptPointContactConstraint::StaticType())
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
						ContactPoint.Normal = Transform1.TransformVectorNoScale(Constraint.GetManifoldPlaneNormal());
						ContactPoint.Location = Transform0.TransformPosition(Location);
					}
				}
			}

			UpdateContactPoint(ConstraintBase.Manifold, ContactPoint);
		}

		template<class T, int d>
		void UpdateConvexConvexConstraintSwept(TGeometryParticleHandle<T, d>* Particle0, const FImplicitObject& Implicit0, const TRigidTransform<T, d>& Transform0, const FImplicitObject& Implicit1, const TRigidTransform<T, d>& Transform1, const TVector<T, d>& Dir, const T Length, const T CullDistance, TRigidBodySweptPointContactConstraint<T, d>& Constraint)
		{
			T TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, ConvexConvexContactPointSwept(Implicit0, Transform0, Implicit1, Transform1, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template<class T, int d>
		void UpdateConvexConvexManifold(TCollisionConstraintBase<T, d>&  ConstraintBase, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance)
		{
			if (ConstraintBase.GetType() == FRigidBodyMultiPointContactConstraint::StaticType())
			{
				TRigidBodyMultiPointContactConstraint<T, d>* Constraint = ConstraintBase.template As<TRigidBodyMultiPointContactConstraint<T, d>>();
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


			EImplicitObjectType Implicit0Type = Particle0->Geometry()->GetType();
			EImplicitObjectType Implicit1Type = Particle1->Geometry()->GetType();

			// Note: This TBox check is a temporary workaround to avoid jitter in cases of Box vs Convex; investigation ongoing
			// We need to improve iterative manifolds for this case
			if (Chaos_Collision_UseManifolds && Implicit0Type != TBox<FReal, 3>::StaticType() && Implicit1Type != TBox<FReal, 3>::StaticType())
			{
				FRigidBodyMultiPointContactConstraint Constraint = FRigidBodyMultiPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexConvex);
				UpdateConvexConvexManifold(Constraint, Transform0, Transform1, CullDistance);
				UpdateConvexConvexConstraint(*Implicit0, Transform0, *Implicit1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
			else
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexConvex);
				UpdateConvexConvexConstraint(*Implicit0, Transform0, *Implicit1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		template<class T, int d>
		void ConstructConvexConvexConstraintsSwept(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, const TVector<T,d>& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			TRigidTransform<T, 3> TransformX0(Particle0->X(), Particle0->R());
			TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
			TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));

			FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexConvex);
			UpdateConvexConvexConstraintSwept(Particle0, *Implicit0, TransformX0, *Implicit1, Transform1, Dir, Length, CullDistance, Constraint);
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

		template <typename T, int d>
		void UpdateConvexHeightFieldConstraintSwept(TGeometryParticleHandle<T, d>* Particle0, const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const THeightField<T>& B, const TRigidTransform<T, d>& BTransform, const TVector<T, d>& Dir, const T Length, const T CullDistance, TRigidBodySweptPointContactConstraint<T, d>& Constraint)
		{
			T TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, GJKImplicitSweptContactPoint< FConvex >(A, ATransform, B, BTransform, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
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
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexHeightField);
				UpdateConvexHeightFieldConstraint(*Implicit0, Transform0, *Object1, Transform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		template<typename T, int d>
		void ConstructConvexHeightFieldConstraintsSwept(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, const TVector<T, d>& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			const THeightField<T> * Object1 = Implicit1->template GetObject<const THeightField<T> >();
			if (ensure(Implicit0->IsConvex() && Object1))
			{
				TRigidTransform<T, 3> TransformX0(Particle0->X(), Particle0->R());
				TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
				TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));

				FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexHeightField);
				UpdateConvexHeightFieldConstraintSwept(Particle0, *Implicit0, TransformX0, *Object1, Transform1, Dir, Length, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Convex-TriangleMesh
		//

		template <typename TriMeshType, typename T, int d>
		TContactPoint<T> ConvexTriangleMeshContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const TriMeshType& B, const TRigidTransform<T, d>& BTransform, const T CullDistance)
		{
			return GJKImplicitContactPoint< FConvex >(A, ATransform, B, BTransform, CullDistance);
		}

		template <typename TriMeshType, typename T, int d>
		TContactPoint<T> ConvexTriangleMeshSweptContactPoint(const FImplicitObject& A, const TRigidTransform<T, d>& ATransform, const TriMeshType& B, const TRigidTransform<T, d>& BStartTransform, const TVector<T, d>& Dir, const T Length, const T CullDistance, T& TOI)
		{
			if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = B.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
			{
				return GJKImplicitScaledTriMeshSweptContactPoint<FConvex>(A, ATransform, *ScaledTriangleMesh, BStartTransform, Dir, Length, CullDistance, TOI);
			}
			else if(const FTriangleMeshImplicitObject* TriangleMesh = B.template GetObject<const FTriangleMeshImplicitObject>())
			{
				return GJKImplicitSweptContactPoint<FConvex>(A, ATransform, *TriangleMesh, BStartTransform, Dir, Length, CullDistance, TOI);
			}

			ensure(false);
			return TContactPoint<T>();
		}
		
		template <typename TriMeshType, typename T, int d>
		void UpdateConvexTriangleMeshConstraint(const FImplicitObject& Convex0, const TRigidTransform<T, d>& Transform0, const TriMeshType& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const T CullDistance, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, ConvexTriangleMeshContactPoint(Convex0, Transform0, TriangleMesh1, Transform1, CullDistance));
		}

		// Sweeps convex against trimesh
		template <typename TriMeshType, typename T, int d>
		void UpdateConvexTriangleMeshConstraintSwept(TGeometryParticleHandle<T,d>* Particle0, const FImplicitObject& Convex0, const TRigidTransform<T, d>& Transform0, const TriMeshType& TriangleMesh1, const TRigidTransform<T, d>& Transform1, const TVector<T, d>& Dir, const T Length, const T CullDistance, TRigidBodySweptPointContactConstraint<T, d>& Constraint)
		{
			T TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, ConvexTriangleMeshSweptContactPoint(Convex0, Transform0, TriangleMesh1, Transform1, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template <typename T, int d>
		void UpdateConvexTriangleMeshManifold(TCollisionConstraintBase<T, d>&  Constraint, const TRigidTransform<T, d>& ATM, const TRigidTransform<T, d>& BTM, const T CullDistance)
		{

		}

		template<typename T, int d>
		void ConstructConvexTriangleMeshConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			if (ensure(Implicit0->IsConvex()))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexTriMesh);
					UpdateConvexTriangleMeshConstraint(*Implicit0, Transform0, *ScaledTriangleMesh , Transform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
					TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexTriMesh);
					UpdateConvexTriangleMeshConstraint(*Implicit0, Transform0, *TriangleMesh, Transform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					ensure(false);
				}
			}
		}

		template<typename T, int d>
		void ConstructConvexTriangleMeshConstraintsSwept(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, const TVector<T, d>& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			TRigidTransform<T, d> ParticleImplicit0TM = Transform0.GetRelativeTransform(Collisions::GetTransform(Particle0));
			TRigidTransform<T, d> ParticleImplicit1TM = Transform1.GetRelativeTransform(Collisions::GetTransform(Particle1));
			if (ensure(Implicit0->IsConvex()))
			{
				TRigidTransform<T, 3> TransformX0(Particle0->X(), Particle0->R());
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexTriMesh);
					UpdateConvexTriangleMeshConstraintSwept(Particle0, *Implicit0, TransformX0, *ScaledTriangleMesh, Transform1, Dir, Length, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::ConvexTriMesh);
					UpdateConvexTriangleMeshConstraintSwept(Particle0, *Implicit0, TransformX0, *TriangleMesh, Transform1, Dir, Length, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					ensure(false);
				}
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
			FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, ParticleImplicit0TM, Particle1, Implicit1, ParticleImplicit1TM, EContactShapesType::LevelSetLevelSet);

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

			UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest>(CullDistance, Constraint);

			NewConstraints.TryAdd(CullDistance, Constraint);
		}


		//
		// Constraint API
		//


		template<typename T, int d>
		inline void UpdateManifold(TRigidBodyMultiPointContactConstraint<T, d>& Constraint, const TRigidTransform<T, d>& ParticleTransform0, const TRigidTransform<T, d>& ParticleTransform1, const T CullDistance, const FCollisionContext& Context)
		{
			const FImplicitObject& Implicit0 = *Constraint.Manifold.Implicit[0];
			const FImplicitObject& Implicit1 = *Constraint.Manifold.Implicit[1];
			const TRigidTransform<T, d> Transform0 = Constraint.ImplicitTransform[0] * ParticleTransform0;
			const TRigidTransform<T, d> Transform1 = Constraint.ImplicitTransform[1] * ParticleTransform1;

			switch (Constraint.Manifold.ShapesType)
			{
			case EContactShapesType::CapsuleBox:
				UpdateCapsuleBoxManifold<T, d>(*Implicit0.template GetObject<const TCapsule<T>>(), Transform0, Implicit1.template GetObject<const TBox<T, d>>()->GetAABB(), Transform1, CullDistance, Context, Constraint);
				break;
			case EContactShapesType::ConvexConvex:
				UpdateConvexConvexManifold(Constraint, Transform0, Transform1, CullDistance);
				break;
			default:
				// This switch needs to contain all pair types that generate a Manifold. ConstructConstrainsImpl
				ensure(false);
				break;
			}

		}

		// Run collision detection for the specified constraint to update the nearest contact point.
		// NOTE: Transforms are world space shape transforms
		// @todo(chaos): use a lookup table?
		// @todo(chaos): add the missing cases below
		// @todo(chaos): see use GetInnerObject below - we should try to uise the leaf types for all (currently only TriMesh needs this)
		template<ECollisionUpdateType UpdateType, typename T, int d>
		inline void UpdateConstraintFromGeometryImpl(TRigidBodyPointContactConstraint<T, d>& Constraint, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance)
		{
			const FImplicitObject& Implicit0 = *Constraint.Manifold.Implicit[0];
			const FImplicitObject& Implicit1 = *Constraint.Manifold.Implicit[1];

			switch (Constraint.Manifold.ShapesType)
			{
			case EContactShapesType::SphereSphere:
				UpdateSphereSphereConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TSphere<T, d>>(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SphereCapsule:
				UpdateSphereCapsuleConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SphereBox:
				UpdateSphereBoxConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SphereConvex:
				// MISSING CASE!!!
				UpdateConvexConvexConstraint(Implicit0, Transform0, Implicit1, Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SphereTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateSphereTriangleMeshConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *ScaledTriMesh, Transform1, CullDistance, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateSphereTriangleMeshConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *TriangleMeshImplicit, Transform1, CullDistance, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::SphereHeightField:
				UpdateSphereHeightFieldConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SpherePlane:
				UpdateSpherePlaneConstraint(*Implicit0.template GetObject<TSphere<T, d>>(), Transform0, *Implicit1.template GetObject<TPlane<T, d>>(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::CapsuleCapsule:
				UpdateCapsuleCapsuleConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject<TCapsule<T>>(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::CapsuleBox:
				UpdateCapsuleBoxConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::CapsuleConvex:
				// MISSING CASE!!!
				UpdateConvexConvexConstraint(Implicit0, Transform0, Implicit1, Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::CapsuleTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateCapsuleTriangleMeshConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *ScaledTriMesh, Transform1, CullDistance, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateCapsuleTriangleMeshConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *TriangleMeshImplicit, Transform1, CullDistance, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::CapsuleHeightField:
				UpdateCapsuleHeightFieldConstraint(*Implicit0.template GetObject<TCapsule<T>>(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::BoxBox:
				UpdateBoxBoxConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, Implicit1.template GetObject<TBox<T, d>>()->GetAABB(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::BoxConvex:
				// MISSING CASE!!!
				UpdateConvexConvexConstraint(Implicit0, Transform0, Implicit1, Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::BoxTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateBoxTriangleMeshConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *ScaledTriMesh, Transform1, CullDistance, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateBoxTriangleMeshConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *TriangleMeshImplicit, Transform1, CullDistance, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::BoxHeightField:
				UpdateBoxHeightFieldConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::BoxPlane:
				UpdateBoxPlaneConstraint(Implicit0.template GetObject<TBox<T, d>>()->GetAABB(), Transform0, *Implicit1.template GetObject<TPlane<T, d>>(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::ConvexConvex:
				UpdateConvexConvexConstraint(Implicit0, Transform0, Implicit1, Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::ConvexTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateConvexTriangleMeshConstraint(Implicit0, Transform0, *ScaledTriMesh, Transform1, CullDistance, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateConvexTriangleMeshConstraint(Implicit0, Transform0, *TriangleMeshImplicit, Transform1, CullDistance, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::ConvexHeightField:
				UpdateConvexHeightFieldConstraint(Implicit0, Transform0, *Implicit1.template GetObject< THeightField<T> >(), Transform1, CullDistance, Constraint);
				break;
			case EContactShapesType::LevelSetLevelSet:
				UpdateLevelsetLevelsetConstraint<UpdateType>(CullDistance, Constraint);
				break;
			default:
				// Switch needs updating....
				ensure(false);
				break;
			}
		}

		template<typename T, int d>
		void ConstructConstraintsImpl(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			// @todo(chaos): We use GetInnerType here because TriMeshes are left with their "Instanced" wrapper, unlike all other instanced implicits. Should we strip the instance on Tri Mesh too?
			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetCollisionType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetCollisionType()) : ImplicitObjectType::Unknown;
			bool bIsConvex0 = Implicit0 && Implicit0->IsConvex();
			bool bIsConvex1 = Implicit1 && Implicit1->IsConvex();

			T LengthCCD = 0.0f;
			TVector<T, d> DirCCD(0.0f);
			bool bUseCCD = UseCCD(Particle0, Particle1, Implicit0, DirCCD, LengthCCD);

			if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == THeightField<FReal>::StaticType())
			{
				ConstructBoxHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == THeightField<FReal>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TPlane<FReal, 3>::StaticType())
			{
				ConstructBoxPlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TPlane<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxPlaneConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereSphereConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == THeightField<FReal>::StaticType())
			{
				ConstructSphereHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == THeightField<FReal>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TPlane<FReal, 3>::StaticType())
			{
				ConstructSpherePlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TPlane<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSpherePlaneConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructSphereBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereBoxConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				ConstructSphereCapsuleConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereCapsuleConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				ConstructCapsuleCapsuleConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructCapsuleBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, Context, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				ConstructCapsuleBoxConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, Context, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == THeightField<FReal>::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleHeightFieldConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == THeightField<FReal>::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleHeightFieldConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructBoxTriangleMeshConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxTriangleMeshConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructSphereTriangleMeshConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereTriangleMeshConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleTriangleMeshConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleTriangleMeshConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (bIsConvex0 && Implicit1Type == THeightField<FReal>::StaticType())
			{
				if (bUseCCD)
				{
					ConstructConvexHeightFieldConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexHeightFieldConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == THeightField<FReal>::StaticType() && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructConvexHeightFieldConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexHeightFieldConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (bIsConvex0 && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				if (bUseCCD)
				{
					ConstructConvexTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexTriangleMeshConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructConvexTriangleMeshConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexTriangleMeshConstraints(Particle1, Particle0, Implicit1, Implicit0, Transform1, Transform0, CullDistance, NewConstraints);
			}
			else if (bIsConvex0 && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructConvexConvexConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexConvexConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
			else
			{
				ConstructLevelsetLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
			}
		}


		// Run collision detection for the specified constraint to update the nearest contact point.
		// NOTE: Transforms are world space particle transforms
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void UpdateConstraintFromGeometry(TRigidBodyPointContactConstraint<T, d>& Constraint, const TRigidTransform<T, d>& ParticleTransform0, const TRigidTransform<T, d>& ParticleTransform1, const T CullDistance)
		{
			const TRigidTransform<T, d> Transform0 = Constraint.ImplicitTransform[0] * ParticleTransform0;
			const TRigidTransform<T, d> Transform1 = Constraint.ImplicitTransform[1] * ParticleTransform1;
			UpdateConstraintFromGeometryImpl<UpdateType>(Constraint, Transform0, Transform1, CullDistance);
		}

		// Select the best manifold point as the new contact point.
		// NOTE: Transforms are world space particle transforms
		template<class T, int d>
		void UpdateConstraintFromManifold(TRigidBodyMultiPointContactConstraint<T, d>& Constraint, const TRigidTransform<T, d>& ParticleTransform0, const TRigidTransform<T, d>& ParticleTransform1, const T CullDistance)
		{
			const FRigidTransform3 Transform0 = Constraint.ImplicitTransform[0] * ParticleTransform0;
			const FRigidTransform3 Transform1 = Constraint.ImplicitTransform[1] * ParticleTransform1;
			const int32 NumPoints = Constraint.NumManifoldPoints();

			// Fall back to full collision detection if we have no manifold (or for testing)
			if ((NumPoints == 0) || Chaos_Collision_UseManifolds_Test)
			{
				UpdateConstraintFromGeometryImpl<ECollisionUpdateType::Deepest>(Constraint, Transform0, Transform1, CullDistance);
				return;
			}

			// Get the plane and point transforms (depends which body owns the plane)
			const FRigidTransform3& PlaneTransform = (Constraint.GetManifoldPlaneOwnerIndex() == 0) ? Transform0 : Transform1;
			const FRigidTransform3& PointsTransform = (Constraint.GetManifoldPlaneOwnerIndex() == 0) ? Transform1 : Transform0;

			// World-space manifold plane
			FVec3 PlaneNormal = PlaneTransform.TransformVectorNoScale(Constraint.GetManifoldPlaneNormal());
			FVec3 PlanePos = PlaneTransform.TransformPosition(Constraint.GetManifoldPlanePosition());

			// Select the best manifold point
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


		typedef uint8 FMaskFilter;
		enum { NumExtraFilterBits = 6 };
		enum { NumCollisionChannelBits = 5 };

		inline bool IsValid(const FCollisionFilterData& Filter)
		{
			return Filter.Word0 || Filter.Word1 || Filter.Word2 || Filter.Word3;
		}

		inline uint32 GetChaosCollisionChannel(uint32 Word3)
		{
			uint32 ChannelMask = (Word3 << NumExtraFilterBits) >> (32 - NumCollisionChannelBits);
			return (uint32)ChannelMask;
		}

		inline uint32 GetChaosCollisionChannelAndExtraFilter(uint32 Word3, FMaskFilter& OutMaskFilter)
		{
			uint32 ChannelMask = GetChaosCollisionChannel(Word3);
			OutMaskFilter = Word3 >> (32 - NumExtraFilterBits);
			return (uint32)ChannelMask;
		}

		inline bool DoCollide(EImplicitObjectType Implicit0Type, const FPerShapeData* Shape0, EImplicitObjectType Implicit1Type, const FPerShapeData* Shape1)
		{
			//
			// Disabled shapes do not collide
			//
			if (Shape0 && (Shape0->GetDisable() || !IsValid(Shape0->GetSimData()) ) ) return false;
			if (Shape1 && (Shape1->GetDisable ()|| !IsValid(Shape1->GetSimData()) ) ) return false;

			//
			// Triangle Mesh geometry is only used if the shape specifies UseComplexAsSimple
			//
			if (Shape0)
			{
				if (Implicit0Type == ImplicitObjectType::TriangleMesh && Shape0->GetCollisionTraceType() != Chaos_CTF_UseComplexAsSimple)
				{
					return false;
				}
				else if (Shape0->GetCollisionTraceType() == Chaos_CTF_UseComplexAsSimple && Implicit0Type != ImplicitObjectType::TriangleMesh)
				{
					return false;
				}
			}
			else if (Implicit0Type == ImplicitObjectType::TriangleMesh)
			{
				return false;
			}

			if (Shape1)
			{
				if (Implicit1Type == ImplicitObjectType::TriangleMesh && Shape1->GetCollisionTraceType() != Chaos_CTF_UseComplexAsSimple)
				{
					return false;
				}
				else if (Shape1->GetCollisionTraceType() == Chaos_CTF_UseComplexAsSimple && Implicit1Type != ImplicitObjectType::TriangleMesh)
				{
					return false;
				}
			}
			else if (Implicit1Type == ImplicitObjectType::TriangleMesh)
			{
				return false;
			}

			//
			// Shape Filtering
			//
			if (Shape0 && Shape1)
			{

				if (IsValid(Shape0->GetSimData()) && IsValid(Shape1->GetSimData()))
				{
					FMaskFilter Filter0Mask, Filter1Mask;
					const uint32 Filter0Channel = GetChaosCollisionChannelAndExtraFilter(Shape0->GetSimData().Word3, Filter0Mask);
					const uint32 Filter1Channel = GetChaosCollisionChannelAndExtraFilter(Shape1->GetSimData().Word3, Filter1Mask);

					if ((Filter0Mask & Filter1Mask) != 0)
					{
						return false;
					}

					const uint32 Filter1Bit = 1 << (Filter1Channel); // SIMDATA_TO_BITFIELD
					uint32 const Filter0Bit = 1 << (Filter0Channel); // SIMDATA_TO_BITFIELD
					return (Filter0Bit & Shape1->GetSimData().Word1) && (Filter1Bit & Shape0->GetSimData().Word1);
				}
			}


			return true;
		}


		template<typename T, int d>
		void ConstructConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<T, d>& Transform0, const TRigidTransform<T, d>& Transform1, const T CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetType()) : ImplicitObjectType::Unknown;


			if (!Implicit0 || !Implicit1)
			{
				ConstructLevelsetLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, NewConstraints);
				return;
			}

			EImplicitObjectType Implicit0OuterType = Implicit0->GetType();
			EImplicitObjectType Implicit1OuterType = Implicit1->GetType();

			// Handle transform wrapper shape
			if ((Implicit0OuterType == TImplicitObjectTransformed<T, d>::StaticType()) && (Implicit1OuterType == TImplicitObjectTransformed<T, d>::StaticType()))
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform0 = TransformedImplicit0->GetTransform() * Transform0;
				TRigidTransform<T, d> TransformedTransform1 = TransformedImplicit1->GetTransform() * Transform1;
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), TransformedImplicit1->GetTransformedObject(), TransformedTransform0, TransformedTransform1, CullDistance, Context, NewConstraints);
				return;
			}
			else if (Implicit0OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform0 = TransformedImplicit0->GetTransform() * Transform0;
				ConstructConstraints(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), Implicit1, TransformedTransform0, Transform1, CullDistance, Context, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectTransformed<T, d>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<T, d> TransformedTransform1 = TransformedImplicit1->GetTransform() * Transform1;
				ConstructConstraints(Particle0, Particle1, Implicit0, TransformedImplicit1->GetTransformedObject(), Transform0, TransformedTransform1, CullDistance, Context, NewConstraints);
				return;
			}
			// Handle Instanced shapes
			// NOTE: Tri Meshes are handled differently. We should probably do something about this...
			if (((uint32)Implicit0OuterType & ImplicitObjectType::IsInstanced) || ((uint32)Implicit1OuterType & ImplicitObjectType::IsInstanced))
			{
				const FImplicitObject* InnerImplicit0 = Implicit0;
				const FImplicitObject* InnerImplicit1 = Implicit1;
				if ((uint32)Implicit0OuterType & ImplicitObjectType::IsInstanced)
				{
					InnerImplicit0 = GetInstancedImplicit(Implicit0);
				}
				if ((uint32)Implicit1OuterType & ImplicitObjectType::IsInstanced)
				{
					InnerImplicit1 = GetInstancedImplicit(Implicit1);
				}
				if (InnerImplicit0 && InnerImplicit1)
				{
					ConstructConstraints(Particle0, Particle1, InnerImplicit0, InnerImplicit1, Transform0, Transform1, CullDistance, Context, NewConstraints);
					return;
				}
				else if (InnerImplicit0 && !InnerImplicit1)
				{
					ConstructConstraints(Particle0, Particle1, InnerImplicit0, Implicit1, Transform0, Transform1, CullDistance, Context, NewConstraints);
					return;
				}
				else if (!InnerImplicit0 && InnerImplicit1)
				{
					ConstructConstraints(Particle0, Particle1, Implicit0, InnerImplicit1, Transform0, Transform1, CullDistance, Context, NewConstraints);
					return;
				}

			}

			// Handle Unions
			if (Implicit0OuterType == FImplicitObjectUnion::StaticType())
			{
				const FImplicitObjectUnion* Union0 = Implicit0->template GetObject<FImplicitObjectUnion>();
				for (const auto& Child0 : Union0->GetObjects())
				{
					ConstructConstraints(Particle0, Particle1, Child0.Get(), Implicit1, Transform0, Transform1, CullDistance, Context, NewConstraints);
				}
				return;
			}
			if (Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				const FImplicitObjectUnion* Union1 = Implicit1->template GetObject<FImplicitObjectUnion>();
				for (const auto& Child1 : Union1->GetObjects())
				{
					ConstructConstraints(Particle0, Particle1, Implicit0, Child1.Get(), Transform0, Transform1, CullDistance, Context, NewConstraints);
				}
				return;
			}

			// Check shape pair filtering if enable
			if (Context.bFilteringEnabled && !DoCollide(Implicit0Type, Particle0->GetImplicitShape(Implicit0), Implicit1Type, Particle1->GetImplicitShape(Implicit1)))
			{
				return;
			}

			// If we get here, we have a pair of concrete shapes (i.e., no wrappers or containers)
			// Create a constraint for the shape pair
			ConstructConstraintsImpl(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, CullDistance, Context, NewConstraints);
		}


		template <typename GeometryA, typename GeometryB>
		bool GetPairTOIHackImpl(const FImplicitObject& A, const FRigidTransform3& AStartTransform, const GeometryB& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, FReal& OutTOI, FVec3& OutNormal, FReal& OutPhi)
		{
			FReal CullDistance = 0.0f;

			FReal TOI = 1.0f;
			TContactPoint<FReal> Contact = GJKImplicitSweptContactPoint<GeometryA>(A, AStartTransform, B, BTransform, Dir, Length, CullDistance, TOI);
			if (Contact.Phi < OutPhi)
			{
				OutPhi = Contact.Phi;
				OutNormal = Contact.Normal;
				OutTOI = TOI;
				return true;
			}
			return false;
		}

		template <typename GeometryA>
		bool GetPairTOIHackTriMeshImpl(const FImplicitObject& A, const FRigidTransform3& AStartTransform, const FImplicitObject* B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, FReal& OutTOI, FVec3& OutNormal, FReal& OutPhi)
		{
			TContactPoint<FReal> Contact;
			FReal TOI = 0.0f;
			FReal CullDistance = 0.0f;

			if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = B->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
			{
				Contact = GJKImplicitScaledTriMeshSweptContactPoint<GeometryA>(A, AStartTransform, *ScaledTriangleMesh, BTransform, Dir, Length, CullDistance, TOI);
			}
			else if (const FTriangleMeshImplicitObject* TriangleMesh = B->template GetObject<const FTriangleMeshImplicitObject>())
			{
				Contact = GJKImplicitSweptContactPoint<GeometryA>(A, AStartTransform, *TriangleMesh, BTransform, Dir, Length, CullDistance, TOI);
			}

			if (Contact.Phi < OutPhi)
			{
				OutPhi = Contact.Phi;
				OutNormal = Contact.Normal;
				OutTOI = TOI;
				return true;
			}
			return false;
		}

		bool GetPairTOIHackConvexConvexImpl(const FImplicitObject& A, const FRigidTransform3& AStartTransform, const FImplicitObject& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, FReal& OutTOI, FVec3& OutNormal, FReal& OutPhi)
		{
			FReal CullDistance = 0.0f;
			FReal TOI = 0.0f;

			TContactPoint<FReal> Contact = ConvexConvexContactPointSwept(A, AStartTransform, B, BTransform, Dir, Length, CullDistance, TOI);
			if (Contact.Phi < OutPhi)
			{
				OutPhi = Contact.Phi;
				OutNormal = Contact.Normal;
				OutTOI = TOI;
				return true;
			}
			return false;
		}



		bool GetPairTOIHack(const TPBDRigidParticleHandle<FReal, 3>* Particle0, const TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& StartTransform0, const FRigidTransform3& Transform1, FReal& OutTOI, FVec3& OutNormal, FReal& OutPhi)
		{
			// It's impossible to access an null Implicit0 or Implicit1 as we check types.
			CA_ASSUME(Implicit0);
			CA_ASSUME(Implicit1);

			if (!Implicit0 || !Implicit1)
			{
				return false;
			}

			// @todo(chaos): We use GetInnerType here because TriMeshes are left with their "Instanced" wrapper, unlike all other instanced implicits. Should we strip the instance on Tri Mesh too?
			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetType()) : ImplicitObjectType::Unknown;
			bool bIsConvex0 = Implicit0 && Implicit0->IsConvex();
			bool bIsConvex1 = Implicit1 && Implicit1->IsConvex();

			FVec3 Dir = Particle0->P() - Particle0->X();
			FReal Length = Dir.Size();
			if (Length < KINDA_SMALL_NUMBER)
			{
				return false;
			}
			Dir /= Length;

			if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == THeightField<FReal>::StaticType())
			{
				return GetPairTOIHackImpl<TBox<FReal, 3>>(*Implicit0->template GetObject<TBox<FReal, 3>>(), StartTransform0, *Implicit1->template GetObject<THeightField<FReal>>(), Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == THeightField<FReal>::StaticType())
			{
				return GetPairTOIHackImpl<TSphere<FReal, 3>>(*Implicit0->template GetObject<TSphere<FReal, 3>>(), StartTransform0, *Implicit1->template GetObject<THeightField<FReal>>(), Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == THeightField<FReal>::StaticType())
			{
				return GetPairTOIHackImpl<TCapsule<FReal>>(*Implicit0->template GetObject<TCapsule<FReal>>(), StartTransform0, *Implicit1->template GetObject<THeightField<FReal>>(), Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				return GetPairTOIHackTriMeshImpl<TBox<FReal, 3>>(*Implicit0->template GetObject<TBox<FReal, 3>>(), StartTransform0, Implicit1, Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				return GetPairTOIHackTriMeshImpl<TSphere<FReal, 3>>(*Implicit0->template GetObject<TSphere<FReal, 3>>(), StartTransform0, Implicit1, Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				return GetPairTOIHackTriMeshImpl<TCapsule<FReal>>(*Implicit0->template GetObject<TCapsule<FReal>>(), StartTransform0, Implicit1, Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (bIsConvex0 && Implicit1Type == THeightField<FReal>::StaticType())
			{
				return GetPairTOIHackImpl<FConvex>(*Implicit0, StartTransform0, *Implicit1->template GetObject<THeightField<FReal>>(), Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (bIsConvex0 && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				return GetPairTOIHackTriMeshImpl<FConvex>(*Implicit0, StartTransform0, Implicit1, Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (bIsConvex0 && bIsConvex1)
			{
				return GetPairTOIHackConvexConvexImpl(*Implicit0, StartTransform0, *Implicit1, Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}

			return false;
		}


		bool GetTOIHackImpl(const TPBDRigidParticleHandle<FReal, 3>* Particle0, const TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, FReal& OutTOI, FVec3& OutNormal, FReal& OutPhi)
		{
			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetType()) : ImplicitObjectType::Unknown;
			
			if (!Implicit0 || !Implicit1)
			{
				return false;
			}

			EImplicitObjectType Implicit0OuterType = Implicit0->GetType();
			EImplicitObjectType Implicit1OuterType = Implicit1->GetType();

			// Handle transform wrapper shape
			if ((Implicit0OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType()) && (Implicit1OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType()))
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<FReal, 3> TransformedTransform0 = TransformedImplicit0->GetTransform() * Transform0;
				TRigidTransform<FReal, 3> TransformedTransform1 = TransformedImplicit1->GetTransform() * Transform1;
				return GetTOIHackImpl(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), TransformedImplicit1->GetTransformedObject(), TransformedTransform0, TransformedTransform1, OutTOI, OutNormal, OutPhi);
			}
			else if (Implicit0OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<FReal, 3> TransformedTransform0 = TransformedImplicit0->GetTransform() * Transform0;
				return GetTOIHackImpl(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), Implicit1, TransformedTransform0, Transform1, OutTOI, OutNormal, OutPhi);
			}
			else if (Implicit1OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				TRigidTransform<FReal, 3> TransformedTransform1 = TransformedImplicit1->GetTransform() * Transform1;
				return GetTOIHackImpl(Particle0, Particle1, Implicit0, TransformedImplicit1->GetTransformedObject(), Transform0, TransformedTransform1, OutTOI, OutNormal, OutPhi);
			}
			// Handle Instanced shapes
			// NOTE: Tri Meshes are handled differently. We should probably do something about this...
			if (((uint32)Implicit0OuterType & ImplicitObjectType::IsInstanced) || ((uint32)Implicit1OuterType & ImplicitObjectType::IsInstanced))
			{
				const FImplicitObject* InnerImplicit0 = Implicit0;
				const FImplicitObject* InnerImplicit1 = Implicit1;
				if ((uint32)Implicit0OuterType & ImplicitObjectType::IsInstanced)
				{
					InnerImplicit0 = GetInstancedImplicit(Implicit0);
				}
				if ((uint32)Implicit1OuterType & ImplicitObjectType::IsInstanced)
				{
					InnerImplicit1 = GetInstancedImplicit(Implicit1);
				}
				if (InnerImplicit0 && InnerImplicit1)
				{
					return GetTOIHackImpl(Particle0, Particle1, InnerImplicit0, InnerImplicit1, Transform0, Transform1, OutTOI, OutNormal, OutPhi);
				}
				else if (InnerImplicit0 && !InnerImplicit1)
				{
					return GetTOIHackImpl(Particle0, Particle1, InnerImplicit0, Implicit1, Transform0, Transform1, OutTOI, OutNormal, OutPhi);
				}
				else if (!InnerImplicit0 && InnerImplicit1)
				{
					return GetTOIHackImpl(Particle0, Particle1, Implicit0, InnerImplicit1, Transform0, Transform1, OutTOI, OutNormal, OutPhi);
				}
			}

			// Handle Unions
			if (Implicit0OuterType == FImplicitObjectUnion::StaticType())
			{
				bool bHit = false;
				const FImplicitObjectUnion* Union0 = Implicit0->template GetObject<FImplicitObjectUnion>();
				for (const auto& Child0 : Union0->GetObjects())
				{
					if (GetTOIHackImpl(Particle0, Particle1, Child0.Get(), Implicit1, Transform0, Transform1, OutTOI, OutNormal, OutPhi))
					{
						bHit = true;
					}
				}
				return bHit;
			}
			if (Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				bool bHit = false;
				const FImplicitObjectUnion* Union1 = Implicit1->template GetObject<FImplicitObjectUnion>();
				for (const auto& Child1 : Union1->GetObjects())
				{
					if (GetTOIHackImpl(Particle0, Particle1, Implicit0, Child1.Get(), Transform0, Transform1, OutTOI, OutNormal, OutPhi))
					{
						bHit = true;
					}
				}
				return bHit;
			}

			// Check shape pair filtering if enable
			if (!DoCollide(Implicit0Type, const_cast<TPBDRigidParticleHandle<FReal, 3>*>(Particle0)->GetImplicitShape(Implicit0), Implicit1Type, const_cast<TGeometryParticleHandle<FReal, 3>*>(Particle1)->GetImplicitShape(Implicit1)))
			{
				return false;
			}

			// If we get here, we have a pair of concrete shapes (i.e., no wrappers or containers)
			// Create a constraint for the shape pair
			return GetPairTOIHack(Particle0, Particle1, Implicit0, Implicit1, Transform0, Transform1, OutTOI, OutNormal, OutPhi);
		}

		bool GetTOIHack(const TPBDRigidParticleHandle<FReal, 3>* Particle0, const TGeometryParticleHandle<FReal, 3>* Particle1, FReal& OutTOI, FVec3& OutNormal, FReal& OutPhi)
		{
			OutPhi = 0.0f;	// Cull Distance

			FRigidTransform3 Particle0StartTransform = FRigidTransform3(Particle0->X(), Particle0->Q());	// Start Pos, Final Rot
			return GetTOIHackImpl(Particle0, Particle1, Particle0->Geometry().Get(), Particle1->Geometry().Get(), Particle0StartTransform, Collisions::GetTransform(Particle1), OutTOI, OutNormal, OutPhi);
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
		template void ConstructCapsuleBoxConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints);

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

		template void UpdateSingleShotManifold<float, 3>(TRigidBodyMultiPointContactConstraint<float, 3>&  Constraint, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void UpdateIterativeManifold<float, 3>(TRigidBodyMultiPointContactConstraint<float, 3>&  Constraint, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void UpdateManifold<float, 3>(TRigidBodyMultiPointContactConstraint<float, 3>& Constraint, const TRigidTransform<float, 3>& ATM, const TRigidTransform<float, 3>& BTM, const float CullDistance, const FCollisionContext& Context);
		template void UpdateConstraintFromGeometry<ECollisionUpdateType::Any, float, 3>(TRigidBodyPointContactConstraint<float, 3>& ConstraintBase, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest, float, 3>(TRigidBodyPointContactConstraint<float, 3>& ConstraintBase, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void UpdateConstraintFromManifold(TRigidBodyMultiPointContactConstraint<float, 3>& Constraint, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance);
		template void ConstructConstraints<float, 3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const TRigidTransform<float, 3>& Transform0, const TRigidTransform<float, 3>& Transform1, const float CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints);

	} // Collisions

} // Chaos
