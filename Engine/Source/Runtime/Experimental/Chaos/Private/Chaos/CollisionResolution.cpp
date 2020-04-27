// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolution.h"

#include "Chaos/CastingUtilities.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Capsule.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
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

#define CHAOS_COLLIDE_CLUSTERED_UNIONS 1

//PRAGMA_DISABLE_OPTIMIZATION

float CCDEnableThresholdBoundsScale = 0.4f;
FAutoConsoleVariableRef  CVarCCDEnableThresholdBoundsScale(TEXT("p.Chaos.CCD.EnableThresholdBoundsScale"), CCDEnableThresholdBoundsScale , TEXT("CCD is used when object position is changing > smallest bound's extent * BoundsScale. 0 will always Use CCD. Values < 0 disables CCD."));

float CCDAllowedDepthBoundsScale = 0.05f;
FAutoConsoleVariableRef CVarCCDAllowedDepthBoundsScale(TEXT("p.Chaos.CCD.AllowedDepthBoundsScale"), CCDAllowedDepthBoundsScale, TEXT("When rolling back to TOI, allow (smallest bound's extent) * AllowedDepthBoundsScale, instead of rolling back to exact TOI w/ penetration = 0."));

// If GJKPenetration returns a phi of abs value < this number, we use PhiWithNormal to resample phi and normal.
// We have observed bad normals coming from GJKPenetration when barely in contact.
#define PHI_RESAMPLE_THRESHOLD 0.001


bool bChaos_Collision_UseManifolds_Test = false;
FAutoConsoleVariableRef CVarChaosCollisionUseManifoldsTest(TEXT("p.Chaos.Collision.UseManifoldsTest"), bChaos_Collision_UseManifolds_Test, TEXT("Enable/Disable use of manifoldes in collision."));

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
		// Data returned by the low-level collision functions
		struct FContactPoint
		{
			FVec3 Normal;
			FVec3 Location;
			FReal Phi;

			FContactPoint() 
			: Normal(1, 0, 0)
			, Phi(TNumericLimits<FReal>::Max()) {}
		};

		// Traits to control how contacts are generated
		template<bool B_IMMEDIATEUPDATE, bool B_ALLOWMANIFOLD>
		struct TConstructCollisionTraits
		{
			// If true, ConstructConstraints also initializes the constraint Phi and Normal based on current state, and
			// contacts beyond CullDistance are culled. If false, a constraint is created for every shape pair.
			// NOTE: Contact Phi and Normal are also calculated at the beginning of each iteration as well. The reason you may
			// want bImmediateUpdate=true is to reduce the number of contacts that make it to the constraint graph (because
			// we have culled those with a separation greater than CullDistance).
			// However, early culling can cause collisions to be missed, e.g., if a joint moves a body into a space where
			// a culled collision should now be active. This can (does) lead to jitter in some cases.
			static const bool bImmediateUpdate = B_IMMEDIATEUPDATE;

			// If true, use contact manifolds where supported. A manifold defines simplified collision between a pair of shapes:
			// a plane on one shape and some points on the other. This makes updating the contact each iteration very fast, but
			// the manifold may not be a good representation if either body moves or rotates too far.
			static const bool bAllowManifold = B_ALLOWMANIFOLD;
		};

		// Determines if body should use CCD. If using CCD, computes Dir and Length of sweep.
		bool UseCCD(const TGeometryParticleHandle<FReal, 3>* SweptParticle, const TGeometryParticleHandle<FReal, 3>* OtherParticle, const FImplicitObject* Implicit, FVec3& Dir, FReal& Length)
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

				FReal MinBoundsAxis = Implicit->BoundingBox().Extents().Min();
				FReal LengthCCDThreshold = MinBoundsAxis * CCDEnableThresholdBoundsScale;

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
		void SetSweptConstraintTOI(TGeometryParticleHandle<FReal, 3>* Particle, const FReal TOI, const FReal Length, const FVec3& Dir, FRigidBodySweptPointContactConstraint& SweptConstraint)
		{
			if (SweptConstraint.Manifold.Phi > 0.0f)
			{
				return;
			}

			TPBDRigidParticleHandle<FReal, 3>* RigidParticle = Particle->CastToRigidParticle();
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


		void UpdateContactPoint(FCollisionContact& Manifold, const FContactPoint& NewContactPoint)
		{
			//for now just override
			if (NewContactPoint.Phi < Manifold.Phi)
			{
				Manifold.Normal = NewContactPoint.Normal;
				Manifold.Location = NewContactPoint.Location;
				Manifold.Phi = NewContactPoint.Phi;
			}
		}

		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKContactPoint2(const GeometryA& A, const GeometryB& B, const FRigidTransform3& ATM, const FRigidTransform3& BToATM, const FVec3& InitialDir)
		{
			SCOPE_CYCLE_COUNTER_GJK();

			FContactPoint Contact;

			FReal Penetration;
			FVec3 ClosestA, ClosestB, Normal;
			int32 NumIterations = 0;

			if (ensure(GJKPenetration<true>(A, B, BToATM, Penetration, ClosestA, ClosestB, Normal, (FReal)0, InitialDir, (FReal)0, &NumIterations)))
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

		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKContactPoint(const GeometryA& A, const FRigidTransform3& ATM, const GeometryB& B, const FRigidTransform3& BTM, const FVec3& InitialDir)
		{
			const FRigidTransform3 BToATM = BTM.GetRelativeTransform(ATM);
			return GJKContactPoint2(A, B, ATM, BToATM, InitialDir);
		}

		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKContactPointSwept(const GeometryA& A, const FRigidTransform3& ATM, const GeometryB& B, const FRigidTransform3& BTM, const FVec3& Dir, const FReal Length, const FReal CullDistance, FReal& TOI)
		{
			FContactPoint Contact;
			const FRigidTransform3 AToBTM = ATM.GetRelativeTransform(BTM);
			const FVec3 LocalDir = BTM.InverseTransformVectorNoScale(Dir);

			FReal OutTime;
			FVec3 Location, Normal;
			int32 NumIterations = 0;

			if (GJKRaycast2(B, A, AToBTM, LocalDir, Length, OutTime, Location, Normal, (FReal)0, true))
			{
				Contact.Location = BTM.TransformPosition(Location);
				Contact.Normal = BTM.TransformVectorNoScale(Normal);
				ComputeSweptContactPhiAndTOIHelper(Contact.Normal, Dir, Length, OutTime, TOI, Contact.Phi);
			}

			return Contact;
		}


		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKImplicitContactPoint(const FImplicitObject& A, const FRigidTransform3& ATransform, const GeometryB& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			FContactPoint Contact;
			const FRigidTransform3 AToBTM = ATransform.GetRelativeTransform(BTransform);

			FReal Penetration = FLT_MAX;
			FVec3 Location, Normal;
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

		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKImplicitSweptContactPoint(const FImplicitObject& A, const FRigidTransform3& AStartTransform, const GeometryB& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, const FReal CullDistance, FReal& TOI)
		{
			FContactPoint Contact;
			const FRigidTransform3 AToBTM = AStartTransform.GetRelativeTransform(BTransform);
			const FVec3 LocalDir = BTransform.InverseTransformVectorNoScale(Dir);

			FReal OutTime = FLT_MAX;
			int32 FaceIndex = -1;
			FVec3 Location, Normal;

			Utilities::CastHelper(A, AStartTransform, [&](const auto& ADowncast, const FRigidTransform3& AFullTM)
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
		template <typename GeometryA>
		FContactPoint GJKImplicitScaledTriMeshSweptContactPoint(const FImplicitObject& A, const FRigidTransform3& AStartTransform, const TImplicitObjectScaled<FTriangleMeshImplicitObject>& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, const FReal CullDistance, FReal& TOI)
		{
			FContactPoint Contact;
			const FRigidTransform3 AToBTM = AStartTransform.GetRelativeTransform(BTransform);
			const FVec3 LocalDir = BTransform.InverseTransformVectorNoScale(Dir);

			if (!ensure(B.GetType() & ImplicitObjectType::TriangleMesh) || !ensure(!IsInstanced(B.GetType())))
			{
				return FContactPoint();
			}

			FReal OutTime = FLT_MAX;
			FVec3 Location, Normal;
			int32 FaceIndex = -1;

			Utilities::CastHelper(A, AStartTransform, [&](const auto& ADowncast, const FRigidTransform3& AFullTM)
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


		FContactPoint ConvexConvexContactPoint(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			FContactPoint ContactPoint = Utilities::CastHelper(A, ATM, [&](const auto& ADowncast, const FRigidTransform3& AFullTM)
			{
				return Utilities::CastHelper(B, BTM, [&](const auto& BDowncast, const FRigidTransform3& BFullTM)
				{
					return GJKContactPoint(ADowncast, AFullTM, BDowncast, BFullTM, FVec3(1, 0, 0));
				});
			});

			if (FMath::Abs(ContactPoint.Phi) < (FReal)(PHI_RESAMPLE_THRESHOLD))
			{
				// If GJKPenetration returns a phi of abs value < this number, we use PhiWithNormal to resample phi and normal.
				// We have observed bad normals coming from GJKPenetration when barely in contact.

				FVec3 ContactLocalB = BTM.InverseTransformPosition(ContactPoint.Location);
				ContactPoint.Phi = B.PhiWithNormal(ContactLocalB, ContactPoint.Normal);
				ContactPoint.Normal = BTM.TransformVectorNoScale(ContactPoint.Normal);
			}

			return ContactPoint;
		}

		FContactPoint ConvexConvexContactPointSwept(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FVec3& Dir, const FReal Length, const FReal CullDistance, FReal& TOI)
		{
			return Utilities::CastHelper(A, ATM, [&](const auto& ADowncast, const FRigidTransform3& AFullTM)
			{
				return Utilities::CastHelper(B, BTM, [&](const auto& BDowncast, const FRigidTransform3& BFullTM)
				{
					return GJKContactPointSwept(ADowncast, AFullTM, BDowncast, BFullTM, Dir, Length, CullDistance, TOI);
				});
			});
		}


		void UpdateSingleShotManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal CullDistance)
		{
			// single shot manifolds for TConvex implicit object in the constraints implicit[0] position. 
			FContactPoint ContactPoint = ConvexConvexContactPoint(*Constraint.Manifold.Implicit[0], WorldTransform0, *Constraint.Manifold.Implicit[1], WorldTransform1, CullDistance);

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
			int32 FaceIndex = Constraint.Manifold.Implicit[0]->FindClosestFaceAndVertices(WorldTransform0.InverseTransformPosition(ContactPoint.Location), CollisionSamples, 1.f);

			bool bNewManifold = (FaceIndex != Constraint.GetManifoldPlaneFaceIndex()) || (Constraint.NumManifoldPoints() == 0);
			if (bNewManifold)
			{
				const FVec3 PlaneNormal = WorldTransform1.InverseTransformVectorNoScale(ContactPoint.Normal);
				const FVec3 PlanePos = WorldTransform1.InverseTransformPosition(ContactPoint.Location - ContactPoint.Phi*ContactPoint.Normal);
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


		void UpdateIterativeManifold(FRigidBodyMultiPointContactConstraint&  Constraint, const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal CullDistance)
		{
			auto SumSampleData = [&](FRigidBodyMultiPointContactConstraint& LambdaConstraint) -> TVector<float, 3>
			{
				TVector<float, 3> ReturnValue(0);
				for (int i = 0; i < LambdaConstraint.NumManifoldPoints(); i++)
				{
					ReturnValue += LambdaConstraint.GetManifoldPoint(i);
				}
				return ReturnValue;
			};

			// iterative manifolds for non TConvex implicit objects that require sampling 
			FContactPoint ContactPoint = ConvexConvexContactPoint(*Constraint.Manifold.Implicit[0], WorldTransform0, *Constraint.Manifold.Implicit[1], WorldTransform1, CullDistance);

			// Cache the nearest point as the initial contact
			Constraint.Manifold.Phi = ContactPoint.Phi;
			Constraint.Manifold.Normal = ContactPoint.Normal;
			Constraint.Manifold.Location = ContactPoint.Location;

			if (!ContactPoint.Normal.Equals(Constraint.GetManifoldPlaneNormal()) || !Constraint.NumManifoldPoints())
			{
				Constraint.ResetManifoldPoints();
				FVec3 PlaneNormal = WorldTransform1.InverseTransformVectorNoScale(ContactPoint.Normal);
				FVec3 PlanePosition = WorldTransform1.InverseTransformPosition(ContactPoint.Location - ContactPoint.Phi*ContactPoint.Normal);
				Constraint.SetManifoldPlane(1, INDEX_NONE, PlaneNormal, PlanePosition);
			}

			FVec3 SurfaceSample = WorldTransform0.InverseTransformPosition(ContactPoint.Location);
			if (Constraint.NumManifoldPoints() < 4)
			{
				Constraint.AddManifoldPoint(SurfaceSample);
			}
			else if (Constraint.NumManifoldPoints() == 4)
			{
				FVec3 Center = SumSampleData(Constraint) / Constraint.NumManifoldPoints();
				FReal Delta = (Center - SurfaceSample).SizeSquared();

				//
				// @todo(chaos) : Collision Manifold
				//    The iterative manifold need to be maximized for area instead of largest 
				//    distance from center.
				//
				FReal SmallestDelta = FLT_MAX;
				int32 SmallestIndex = 0;
				for (int32 idx = 0; idx < Constraint.NumManifoldPoints(); idx++)
				{
					FReal IdxDelta = (Center - Constraint.GetManifoldPoint(idx)).SizeSquared();
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

		FContactPoint BoxBoxContactPoint(const FAABB3& Box1, const FAABB3& Box2, const FRigidTransform3& ATM, const FRigidTransform3& BToATM, const FReal CullDistance)
		{
			return GJKContactPoint2(Box1, Box2, ATM, BToATM, FVec3(1, 0, 0));
		}


		void UpdateBoxBoxConstraint(const FAABB3& Box1, const FRigidTransform3& Box1Transform, const FAABB3& Box2, const FRigidTransform3& Box2Transform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			const FRigidTransform3 Box2ToBox1TM = Box2Transform.GetRelativeTransform(Box1Transform);
			FAABB3 Box2In1 = Box2.TransformedAABB(Box2ToBox1TM);
			Box2In1.Thicken(CullDistance);
			if (Box1.Intersects(Box2In1))
			{
				UpdateContactPoint(Constraint.Manifold, BoxBoxContactPoint(Box1, Box2, Box1Transform, Box2ToBox1TM, CullDistance));
			}
		}


		void UpdateBoxBoxManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructBoxBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TBox<FReal, 3> * Object0 = Implicit0->template GetObject<const TBox<FReal, 3> >();
			const TBox<FReal, 3> * Object1 = Implicit1->template GetObject<const TBox<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::BoxBox);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateBoxBoxConstraint(Object0->BoundingBox(), WorldTransform0, Object1->BoundingBox(), WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		//
		// Box - HeightField
		//


		FContactPoint BoxHeightFieldContactPoint(const FAABB3& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			return GJKImplicitContactPoint< TBox<float, 3> >(TBox<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}


		void UpdateBoxHeightFieldConstraint(const FAABB3& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, BoxHeightFieldContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		void UpdateBoxHeightFieldManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructBoxHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TBox<FReal, 3> * Object0 = Implicit0->template GetObject<const TBox<FReal, 3> >();
			const FHeightField * Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::BoxHeightField);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateBoxHeightFieldConstraint(Object0->BoundingBox(), WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}



		//
		// Box-Plane
		//

		void UpdateBoxPlaneConstraint(const FAABB3& Box, const FRigidTransform3& BoxTransform, const TPlane<FReal, 3>& Plane, const FRigidTransform3& PlaneTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			FCollisionContact & Contact = Constraint.Manifold;

#if USING_CODE_ANALYSIS
			MSVC_PRAGMA(warning(push))
				MSVC_PRAGMA(warning(disable : ALL_CODE_ANALYSIS_WARNINGS))
#endif	// USING_CODE_ANALYSIS

			const FRigidTransform3 BoxToPlaneTransform(BoxTransform.GetRelativeTransform(PlaneTransform));
			const FVec3 Extents = Box.Extents();
			constexpr int32 NumCorners = 2 + 2 * 3;
			constexpr FReal Epsilon = KINDA_SMALL_NUMBER;

			FVec3 Corners[NumCorners];
			int32 CornerIdx = 0;
			Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max());
			Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min());
			for (int32 j = 0; j < 3; ++j)
			{
				Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min() + FVec3::AxisVector(j) * Extents);
				Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max() - FVec3::AxisVector(j) * Extents);
			}

#if USING_CODE_ANALYSIS
			MSVC_PRAGMA(warning(pop))
#endif	// USING_CODE_ANALYSIS

			FVec3 PotentialConstraints[NumCorners];
			int32 NumConstraints = 0;
			for (int32 i = 0; i < NumCorners; ++i)
			{
				FVec3 Normal;
				const FReal NewPhi = Plane.PhiWithNormal(Corners[i], Normal);
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
				FVec3 AverageLocation(0);
				for (int32 ConstraintIdx = 0; ConstraintIdx < NumConstraints; ++ConstraintIdx)
				{
					AverageLocation += PotentialConstraints[ConstraintIdx];
				}
				Contact.Location = AverageLocation / NumConstraints;
			}
		}

		void UpdateBoxPlaneManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}


		template<typename T_TRAITS>
		void ConstructBoxPlaneConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TBox<FReal, 3> * Object0 = Implicit0->template GetObject<const TBox<FReal, 3> >();
			const TPlane<FReal, 3>* Object1 = Implicit1->template GetObject<const TPlane<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::BoxPlane);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateBoxPlaneConstraint(Object0->BoundingBox(), WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		//
		// Box-TriangleMesh
		//

		template <typename TriMeshType>
		FContactPoint BoxTriangleMeshContactPoint(const FAABB3& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			return GJKImplicitContactPoint< TBox<float, 3> >(TBox<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}

		template <typename TriMeshType>
		void UpdateBoxTriangleMeshConstraint(const FAABB3& Box0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, BoxTriangleMeshContactPoint(Box0, WorldTransform0, TriangleMesh1, WorldTransform1, CullDistance));
		}



		void UpdateBoxTriangleMeshManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{

		}

		template<typename T_TRAITS>
		void ConstructBoxTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)		{
			const TBox<FReal, 3> * Object0 = Implicit0->template GetObject<const TBox<FReal, 3> >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::BoxTriMesh);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateBoxTriangleMeshConstraint(Object0->GetAABB(), WorldTransform0, *ScaledTriangleMesh, WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
				}
				else if(const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::BoxTriMesh);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateBoxTriangleMeshConstraint(Object0->GetAABB(), WorldTransform0, *TriangleMesh, WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
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


		FContactPoint SphereSphereContactPoint(const TSphere<FReal, 3>& Sphere1, const FRigidTransform3& Sphere1Transform, const TSphere<FReal, 3>& Sphere2, const FRigidTransform3& Sphere2Transform, const FReal CullDistance)
		{
			FContactPoint Result;

			const FVec3 Center1 = Sphere1Transform.TransformPosition(Sphere1.GetCenter());
			const FVec3 Center2 = Sphere2Transform.TransformPosition(Sphere2.GetCenter());
			const FVec3 Direction = Center1 - Center2;
			const FReal Size = Direction.Size();
			const FReal NewPhi = Size - (Sphere1.GetRadius() + Sphere2.GetRadius());
			Result.Phi = NewPhi;
			Result.Normal = Size > SMALL_NUMBER ? Direction / Size : FVec3(0, 0, 1);
			Result.Location = Center1 - Sphere1.GetRadius() * Result.Normal;

			return Result;
		}


		void UpdateSphereSphereConstraint(const TSphere<FReal, 3>& Sphere1, const FRigidTransform3& Sphere1Transform, const TSphere<FReal, 3>& Sphere2, const FRigidTransform3& Sphere2Transform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereSphereContactPoint(Sphere1, Sphere1Transform, Sphere2, Sphere2Transform, CullDistance));
		}

		void UpdateSphereSphereManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructSphereSphereConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const TSphere<FReal, 3>* Object1 = Implicit1->template GetObject<const TSphere<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::SphereSphere);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereSphereConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		//
		// Sphere - HeightField
		//

		FContactPoint SphereHeightFieldContactPoint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			return GJKImplicitContactPoint< TSphere<float, 3> >(TSphere<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}


		void UpdateSphereHeightFieldConstraint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereHeightFieldContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		void UpdateSphereHeightFieldManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructSphereHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const FHeightField * Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::SphereHeightField);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereHeightFieldConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		//
		//  Sphere-Plane
		//

		void UpdateSpherePlaneConstraint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const TPlane<FReal, 3>& Plane, const FRigidTransform3& PlaneTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			FCollisionContact & Contact = Constraint.Manifold;

			const FRigidTransform3 SphereToPlaneTransform(PlaneTransform.Inverse() * SphereTransform);
			const FVec3 SphereCenter = SphereToPlaneTransform.TransformPosition(Sphere.GetCenter());

			FVec3 NewNormal;
			FReal NewPhi = Plane.PhiWithNormal(SphereCenter, NewNormal);
			NewPhi -= Sphere.GetRadius();

			if (NewPhi < Contact.Phi)
			{
				Contact.Phi = NewPhi;
				Contact.Normal = PlaneTransform.TransformVectorNoScale(NewNormal);
				Contact.Location = SphereCenter - Contact.Normal * Sphere.GetRadius();
			}
		}

		void UpdateSpherePlaneManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructSpherePlaneConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const TPlane<FReal, 3>* Object1 = Implicit1->template GetObject<const TPlane<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::SpherePlane);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSpherePlaneConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		//
		// Sphere - Box
		//


		FContactPoint SphereBoxContactPoint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FAABB3& Box, const FRigidTransform3& BoxTransform, const FReal CullDistance)
		{
			FContactPoint Result;

			const FRigidTransform3 SphereToBoxTransform(SphereTransform * BoxTransform.Inverse());	//todo: this should use GetRelative
			const FVec3 SphereCenterInBox = SphereToBoxTransform.TransformPosition(Sphere.GetCenter());

			FVec3 NewNormal;
			FReal NewPhi = Box.PhiWithNormal(SphereCenterInBox, NewNormal);
			NewPhi -= Sphere.GetRadius();

			Result.Phi = NewPhi;
			Result.Normal = BoxTransform.TransformVectorNoScale(NewNormal);
			Result.Location = SphereTransform.TransformPosition(Sphere.GetCenter()) - Result.Normal * Sphere.GetRadius();
			return Result;
		}


		void UpdateSphereBoxConstraint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FAABB3& Box, const FRigidTransform3& BoxTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereBoxContactPoint(Sphere, SphereTransform, Box, BoxTransform, CullDistance));
		}

		void UpdateSphereBoxManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructSphereBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const TBox<FReal, 3> * Object1 = Implicit1->template GetObject<const TBox<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::SphereBox);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereBoxConstraint(*Object0, WorldTransform0, Object1->BoundingBox(), WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}


		//
		// Sphere - Capsule
		//

		FContactPoint SphereCapsuleContactPoint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const TCapsule<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			FContactPoint Result;

			FVector A1 = ATransform.TransformPosition(A.GetCenter());
			FVector B1 = BTransform.TransformPosition(B.GetX1());
			FVector B2 = BTransform.TransformPosition(B.GetX2());
			FVector P2 = FMath::ClosestPointOnSegment(A1, B1, B2);

			FVec3 Delta = P2 - A1;
			FReal DeltaLen = Delta.Size();
			if (DeltaLen > KINDA_SMALL_NUMBER)
			{
				FReal NewPhi = DeltaLen - (A.GetRadius() + B.GetRadius());
				FVec3 Dir = Delta / DeltaLen;
				Result.Phi = NewPhi;
				Result.Normal = -Dir;
				Result.Location = A1 + Dir * A.GetRadius();
			}

			return Result;
		}


		void UpdateSphereCapsuleConstraint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const TCapsule<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereCapsuleContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		void UpdateSphereCapsuleManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructSphereCapsuleConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const TCapsule<FReal>* Object1 = Implicit1->template GetObject<const TCapsule<FReal> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::SphereCapsule);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereCapsuleConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		//
		// Sphere-TriangleMesh
		//

		template <typename TriMeshType>
		FContactPoint SphereTriangleMeshContactPoint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			return GJKImplicitContactPoint< TSphere<float, 3> >(TSphere<float, 3>(A), ATransform, B, BTransform, CullDistance);
		}

		template <typename TriMeshType>
		void UpdateSphereTriangleMeshConstraint(const TSphere<FReal, 3>& Sphere0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, SphereTriangleMeshContactPoint(Sphere0, WorldTransform0, TriangleMesh1, WorldTransform1, CullDistance));
		}



		void UpdateSphereTriangleMeshManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{

		}

		template<typename T_TRAITS>
		void ConstructSphereTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::SphereTriMesh);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateSphereTriangleMeshConstraint(*Object0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::SphereTriMesh);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateSphereTriangleMeshConstraint(*Object0, WorldTransform0, *TriangleMesh, WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
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


		FContactPoint CapsuleCapsuleContactPoint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const TCapsule<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			FContactPoint Result;

			FVector A1 = ATransform.TransformPosition(A.GetX1());
			FVector A2 = ATransform.TransformPosition(A.GetX2());
			FVector B1 = BTransform.TransformPosition(B.GetX1());
			FVector B2 = BTransform.TransformPosition(B.GetX2());
			FVector P1, P2;
			FMath::SegmentDistToSegmentSafe(A1, A2, B1, B2, P1, P2);

			FVec3 Delta = P2 - P1;
			FReal DeltaLen = Delta.Size();
			if (DeltaLen > KINDA_SMALL_NUMBER)
			{
				FReal NewPhi = DeltaLen - (A.GetRadius() + B.GetRadius());
				FVec3 Dir = Delta / DeltaLen;
				Result.Phi = NewPhi;
				Result.Normal = -Dir;
				Result.Location = P1 + Dir * A.GetRadius();
			}

			return Result;
		}


		void UpdateCapsuleCapsuleConstraint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const TCapsule<FReal>& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleCapsuleContactPoint(A, ATransform, B, BTransform, CullDistance));
		}

		void UpdateCapsuleCapsuleManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<FReal> * Object0 = Implicit0->template GetObject<const TCapsule<FReal> >();
			const TCapsule<FReal> * Object1 = Implicit1->template GetObject<const TCapsule<FReal> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleCapsule);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleCapsuleConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		//
		// Capsule - Box
		//


		FContactPoint CapsuleBoxContactPoint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const FAABB3& B, const FRigidTransform3& BTransform, const FVec3& InitialDir, const FReal CullDistance)
		{
			return GJKContactPoint(A, ATransform, B, BTransform, InitialDir);
		}


		void UpdateCapsuleBoxConstraint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const FAABB3& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(ccaulfield): the inverse of CapsuleToBoxTM is calculated in GJKContactPoint - try to eliminate this one
			const FRigidTransform3 CapsuleToBoxTM = ATransform.GetRelativeTransform(BTransform);
			const FVec3 P1 = CapsuleToBoxTM.TransformPosition(A.GetX1());
			const FVec3 P2 = CapsuleToBoxTM.TransformPosition(A.GetX2());
			FAABB3 CapsuleAABB(P1.ComponentMin(P2), P1.ComponentMax(P2));
			CapsuleAABB.Thicken(A.GetRadius() + CullDistance);
			if (CapsuleAABB.Intersects(B))
			{
				const FVec3 InitialDir = ATransform.GetRotation().Inverse() * -Constraint.GetNormal();
				UpdateContactPoint(Constraint.Manifold, CapsuleBoxContactPoint(A, ATransform, B, BTransform, InitialDir, CullDistance));
			}
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
		void UpdateCapsuleBoxManifold(const TCapsule<FReal>& Capsule, const FRigidTransform3& CapsuleTM, const FAABB3& Box, const FRigidTransform3& BoxTM, const FReal CullDistance, const FCollisionContext& Context, FRigidBodyMultiPointContactConstraint& Constraint)
		{
			Constraint.ResetManifoldPoints();

			// Find the nearest points on the capsule and box
			// Note: We flip the order for GJK so we get the normal in box space. This makes it easier to build the face-capsule manifold.
			const FRigidTransform3 CapsuleToBoxTM = CapsuleTM.GetRelativeTransform(BoxTM);

			// NOTE: All GJK results in box-space
			// @todo(ccaulfield): use center-to-center direction for InitialDir
			FVec3 InitialDir = FVec3(1, 0, 0);
			FReal Penetration;
			FVec3 CapsuleClosestBoxSpace, BoxClosestBoxSpace, NormalBoxSpace;
			{
				SCOPE_CYCLE_COUNTER_GJK();
				if (!ensure(GJKPenetration<true>(Box, Capsule, CapsuleToBoxTM, Penetration, BoxClosestBoxSpace, CapsuleClosestBoxSpace, NormalBoxSpace, (FReal)0, InitialDir, (FReal)0)))
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

		template<typename T_TRAITS>
		void ConstructCapsuleBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<FReal> * Object0 = Implicit0->template GetObject<const TCapsule<FReal>>();
			const TBox<FReal, 3> * Object1 = Implicit1->template GetObject<const TBox<FReal, 3>>();
			if (ensure(Object0 && Object1))
			{
				if (T_TRAITS::bAllowManifold)
				{
					bool bAllowManifold = true;
					if (Chaos_Collision_CapsuleBoxManifoldTolerance > KINDA_SMALL_NUMBER)
					{
						// @todo(ccaulfield): weak sauce - fix capsule-box manifolds.
						// HACK: Disable manifolds for "horizontal" capsules. Manifolds don't work well when joints are pulling boxes down
						// (under gravity) when the upper boxes are draped over a horizontal capsule. The box rotations about the manifold
						// points(line) is too great and we end up with jitter.
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						const FVector CapsuleAxis = Context.SpaceTransform.TransformVectorNoScale(WorldTransform0.TransformVectorNoScale(Object0->GetAxis()));
						bAllowManifold = (FMath::Abs(CapsuleAxis.Z) > Chaos_Collision_CapsuleBoxManifoldTolerance);
					}

					if (bAllowManifold)
					{
						FRigidBodyMultiPointContactConstraint Constraint = FRigidBodyMultiPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleBox);
						// @todo(ccaulfield): manifold creation should be deferrable same as non-manifold below (only Context is preventing this - Apply does not know about the context yet)
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateCapsuleBoxManifold(*Object0, WorldTransform0, Object1->BoundingBox(), WorldTransform1, CullDistance, Context, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
						return;
					}
				}

				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleBox);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleBoxConstraint(*Object0, WorldTransform0, Object1->BoundingBox(), WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		//
		// Capsule-HeightField
		//


		FContactPoint CapsuleHeightFieldContactPoint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			return GJKImplicitContactPoint< TCapsule<float> >(A, ATransform, B, BTransform, CullDistance);
		}


		void UpdateCapsuleHeightFieldConstraint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleHeightFieldContactPoint(A, ATransform, B, BTransform, CullDistance));
		}


		void UpdateCapsuleHeightFieldConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, const FReal CullDistance, FRigidBodySweptPointContactConstraint& Constraint)
		{
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, GJKImplicitSweptContactPoint<TCapsule<float> >(A, ATransform, B, BTransform, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		void UpdateCapsuleHeightFieldManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructCapsuleHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{

			const TCapsule<FReal> * Object0 = Implicit0->template GetObject<const TCapsule<FReal> >();
			const FHeightField * Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleHeightField);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleHeightFieldConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		void ConstructCapsuleHeightFieldConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<FReal> * Object0 = Implicit0->template GetObject<const TCapsule<FReal> >();
			const FHeightField * Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleHeightField);
				FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
				FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
				UpdateCapsuleHeightFieldConstraintSwept(Particle0, *Object0, WorldTransformX0, *Object1, WorldTransform1, Dir, Length, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}


		//
		// Capsule-TriangleMesh
		//

		template <typename TriMeshType>
		FContactPoint CapsuleTriangleMeshContactPoint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			return GJKImplicitContactPoint< TCapsule<FReal> >(A, ATransform, B, BTransform, CullDistance);
		}

		template <typename TriMeshType>
		FContactPoint CapsuleTriangleMeshSweptContactPoint(const TCapsule<FReal>& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BStartTransform, const FVec3& Dir, const FReal Length, const FReal CullDistance, FReal& TOI)
		{
			if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = B.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
			{
				return GJKImplicitScaledTriMeshSweptContactPoint<TCapsule<FReal>>(A, ATransform, *ScaledTriangleMesh, BStartTransform, Dir, Length, CullDistance, TOI);
			}
			else if (const FTriangleMeshImplicitObject* TriangleMesh = B.template GetObject<const FTriangleMeshImplicitObject>())
			{
				return GJKImplicitSweptContactPoint<TCapsule<FReal>>(A, ATransform, *TriangleMesh, BStartTransform, Dir, Length, CullDistance, TOI);
			}

			ensure(false);
			return FContactPoint();
		}


		template <typename TriMeshType>
		void UpdateCapsuleTriangleMeshConstraint(const TCapsule<FReal>& Capsule0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, CapsuleTriangleMeshContactPoint(Capsule0, WorldTransform0, TriangleMesh1, WorldTransform1, CullDistance));
		}

		template <typename TriMeshType>
		void UpdateCapsuleTriangleMeshConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const TCapsule<FReal>& Capsule0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FVec3& Dir, const FReal Length, const FReal CullDistance, FRigidBodySweptPointContactConstraint& Constraint)
		{
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, CapsuleTriangleMeshSweptContactPoint(Capsule0, WorldTransform0, TriangleMesh1, WorldTransform1, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}


		void UpdateCapsuleTriangleMeshManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{

		}

		template<typename T_TRAITS>
		void ConstructCapsuleTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<FReal> * Object0 = Implicit0->template GetObject<const TCapsule<FReal> >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleTriMesh);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateCapsuleTriangleMeshConstraint(*Object0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleTriMesh);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateCapsuleTriangleMeshConstraint(*Object0, WorldTransform0, *TriangleMesh, WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
				}
				else
				{
					ensure(false);
				}
			}
		}

		void ConstructCapsuleTriangleMeshConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			const TCapsule<FReal> * Object0 = Implicit0->template GetObject<const TCapsule<FReal> >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransformX0, *ScaledTriangleMesh, WorldTransform1, Dir, Length, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::CapsuleTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransformX0, *TriangleMesh, WorldTransform1, Dir, Length, CullDistance, Constraint);
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

		void UpdateConvexConvexConstraint(const FImplicitObject& Implicit0, const FRigidTransform3& WorldTransform0, const FImplicitObject& Implicit1, const FRigidTransform3& WorldTransform1, const FReal CullDistance, FCollisionConstraintBase& ConstraintBase)
		{
			FContactPoint ContactPoint;

			if (ConstraintBase.GetType() == FRigidBodyPointContactConstraint::StaticType())
			{
				ContactPoint = ConvexConvexContactPoint(Implicit0, WorldTransform0, Implicit1, WorldTransform1, CullDistance);
			}
			else if (ConstraintBase.GetType() == FRigidBodySweptPointContactConstraint::StaticType())
			{
				ContactPoint = ConvexConvexContactPoint(Implicit0, WorldTransform0, Implicit1, WorldTransform1, CullDistance);
			}
			else if (ConstraintBase.GetType() == FRigidBodyMultiPointContactConstraint::StaticType())
			{
				FRigidBodyMultiPointContactConstraint& Constraint = *ConstraintBase.template As<FRigidBodyMultiPointContactConstraint>();
				ContactPoint.Phi = FLT_MAX;

				const FRigidTransform3 AToBTM = WorldTransform0.GetRelativeTransform(WorldTransform1);

				TPlane<FReal, 3> CollisionPlane(Constraint.GetManifoldPlanePosition(), Constraint.GetManifoldPlaneNormal());

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
						ContactPoint.Normal = WorldTransform1.TransformVectorNoScale(Constraint.GetManifoldPlaneNormal());
						ContactPoint.Location = WorldTransform0.TransformPosition(Location);
					}
				}
			}

			UpdateContactPoint(ConstraintBase.Manifold, ContactPoint);
		}

		void UpdateConvexConvexConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const FImplicitObject& Implicit0, const FRigidTransform3& WorldTransform0, const FImplicitObject& Implicit1, const FRigidTransform3& WorldTransform1, const FVec3& Dir, const FReal Length, const FReal CullDistance, FRigidBodySweptPointContactConstraint& Constraint)
		{
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, ConvexConvexContactPointSwept(Implicit0, WorldTransform0, Implicit1, WorldTransform1, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		void UpdateConvexConvexManifold(FCollisionConstraintBase&  ConstraintBase, const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal CullDistance)
		{
			if (ConstraintBase.GetType() == FRigidBodyMultiPointContactConstraint::StaticType())
			{
				FRigidBodyMultiPointContactConstraint* Constraint = ConstraintBase.template As<FRigidBodyMultiPointContactConstraint>();
				if (GetInnerType(ConstraintBase.Manifold.Implicit[0]->GetType()) == ImplicitObjectType::Convex)
				{
					UpdateSingleShotManifold(*Constraint, WorldTransform0, WorldTransform1, CullDistance);
				}
				else
				{
					UpdateIterativeManifold(*Constraint, WorldTransform0, WorldTransform1, CullDistance);
				}
			}
		}


		template<typename T_TRAITS>
		void ConstructConvexConvexConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			EImplicitObjectType Implicit0Type = Particle0->Geometry()->GetType();
			EImplicitObjectType Implicit1Type = Particle1->Geometry()->GetType();

			if (T_TRAITS::bAllowManifold)
			{
				// Note: This TBox check is a temporary workaround to avoid jitter in cases of Box vs Convex; investigation ongoing
				// We need to improve iterative manifolds for this case
				if (Implicit0Type != TBox<FReal, 3>::StaticType() && Implicit1Type != TBox<FReal, 3>::StaticType())
				{
					FRigidBodyMultiPointContactConstraint Constraint = FRigidBodyMultiPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::ConvexConvex);
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateConvexConvexManifold(Constraint, WorldTransform0, WorldTransform1, CullDistance);
					if (T_TRAITS::bImmediateUpdate)
					{
						UpdateConvexConvexConstraint(*Implicit0, WorldTransform0, *Implicit1, WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
					return;
				}
			}

			FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::ConvexConvex);
			if (T_TRAITS::bImmediateUpdate)
			{
				FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
				FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
				UpdateConvexConvexConstraint(*Implicit0, WorldTransform0, *Implicit1, WorldTransform1, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
			else
			{
				NewConstraints.Add(Constraint);
			}
		}

		void ConstructConvexConvexConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::ConvexConvex);
			FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
			FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
			UpdateConvexConvexConstraintSwept(Particle0, *Implicit0, WorldTransformX0, *Implicit1, WorldTransform1, Dir, Length, CullDistance, Constraint);
			NewConstraints.TryAdd(CullDistance, Constraint);
		}

		//
		// Convex - HeightField
		//


		FContactPoint ConvexHeightFieldContactPoint(const FImplicitObject& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			return GJKImplicitContactPoint< FConvex >(A, ATransform, B, BTransform, CullDistance);
		}


		void UpdateConvexHeightFieldConstraint(const FImplicitObject& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, ConvexHeightFieldContactPoint(A, ATransform, B, BTransform, CullDistance));
		}


		void UpdateConvexHeightFieldConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const FImplicitObject& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, const FReal CullDistance, FRigidBodySweptPointContactConstraint& Constraint)
		{
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, GJKImplicitSweptContactPoint< FConvex >(A, ATransform, B, BTransform, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		void UpdateConvexHeightFieldManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}

		template<typename T_TRAITS>
		void ConstructConvexHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			const FHeightField * Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Implicit0->IsConvex() && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::ConvexHeightField);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateConvexHeightFieldConstraint(*Implicit0, WorldTransform0, *Object1, WorldTransform1, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else
				{
					NewConstraints.Add(Constraint);
				}
			}
		}

		void ConstructConvexHeightFieldConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			const FHeightField * Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Implicit0->IsConvex() && Object1))
			{
				FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::ConvexHeightField);
				FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
				FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
				UpdateConvexHeightFieldConstraintSwept(Particle0, *Implicit0, WorldTransformX0, *Object1, WorldTransform1, Dir, Length, CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
		}

		//
		// Convex-TriangleMesh
		//

		template <typename TriMeshType>
		FContactPoint ConvexTriangleMeshContactPoint(const FImplicitObject& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BTransform, const FReal CullDistance)
		{
			return GJKImplicitContactPoint< FConvex >(A, ATransform, B, BTransform, CullDistance);
		}

		template <typename TriMeshType>
		FContactPoint ConvexTriangleMeshSweptContactPoint(const FImplicitObject& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BStartTransform, const FVec3& Dir, const FReal Length, const FReal CullDistance, FReal& TOI)
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
			return FContactPoint();
		}
		
		template <typename TriMeshType>
		void UpdateConvexTriangleMeshConstraint(const FImplicitObject& Convex0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint.Manifold, ConvexTriangleMeshContactPoint(Convex0, WorldTransform0, TriangleMesh1, WorldTransform1, CullDistance));
		}

		// Sweeps convex against trimesh
		template <typename TriMeshType>
		void UpdateConvexTriangleMeshConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const FImplicitObject& Convex0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FVec3& Dir, const FReal Length, const FReal CullDistance, FRigidBodySweptPointContactConstraint& Constraint)
		{
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint.Manifold, ConvexTriangleMeshSweptContactPoint(Convex0, WorldTransform0, TriangleMesh1, WorldTransform1, Dir, Length, CullDistance, TOI));
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}


		void UpdateConvexTriangleMeshManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{

		}

		template<typename T_TRAITS>
		void ConstructConvexTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			if (ensure(Implicit0->IsConvex()))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform0, EContactShapesType::ConvexTriMesh);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateConvexTriangleMeshConstraint(*Implicit0, WorldTransform0, *ScaledTriangleMesh , WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::ConvexTriMesh);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateConvexTriangleMeshConstraint(*Implicit0, WorldTransform0, *TriangleMesh, WorldTransform1, CullDistance, Constraint);
						NewConstraints.TryAdd(CullDistance, Constraint);
					}
					else
					{
						NewConstraints.Add(Constraint);
					}
				}
				else
				{
					ensure(false);
				}
			}
		}

		void ConstructConvexTriangleMeshConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			if (ensure(Implicit0->IsConvex()))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::ConvexTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateConvexTriangleMeshConstraintSwept(Particle0, *Implicit0, WorldTransformX0, *ScaledTriangleMesh, WorldTransform1, Dir, Length, CullDistance, Constraint);
					NewConstraints.TryAdd(CullDistance, Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::ConvexTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateConvexTriangleMeshConstraintSwept(Particle0, *Implicit0, WorldTransformX0, *TriangleMesh, WorldTransform1, Dir, Length, CullDistance, Constraint);
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
		template<ECollisionUpdateType UpdateType>
		void UpdateLevelsetLevelsetConstraint(const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetLevelsetConstraint);

			TGenericParticleHandle<FReal, 3> Particle0 = Constraint.Particle[0];
			FRigidTransform3 ParticlesTM = FRigidTransform3(Particle0->P(), Particle0->Q());
			if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
			{
				return;
			}

			TGenericParticleHandle<FReal, 3> Particle1 = Constraint.Particle[1];
			FRigidTransform3 LevelsetTM = Constraint.ImplicitTransform[1] * FRigidTransform3(Particle1->P(), Particle1->Q());
			if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
			{
				return;
			}

			const TBVHParticles<FReal, 3>* SampleParticles = nullptr;
			SampleParticles = Particle0->CollisionParticles().Get();

			if (SampleParticles)
			{
				const FImplicitObject* Obj1 = Constraint.Manifold.Implicit[1];
				SampleObject<UpdateType>(*Obj1, LevelsetTM, *SampleParticles, ParticlesTM, CullDistance, Constraint);
			}
		}

		void UpdateLevelsetLevelsetManifold(FCollisionConstraintBase&  Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM, const FReal CullDistance)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}


		template<typename T_TRAITS>
		void ConstructLevelsetLevelsetConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FCollisionConstraintsArray& NewConstraints)
		{
			FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, LocalTransform0, Particle1, Implicit1, LocalTransform1, EContactShapesType::LevelSetLevelSet);

			bool bIsParticleDynamic0 = Particle0->CastToRigidParticle() && Particle0->ObjectState() == EObjectStateType::Dynamic;
			if (!Particle1->Geometry() || (bIsParticleDynamic0 && !Particle0->CastToRigidParticle()->CollisionParticlesSize() && Particle0->Geometry() && !Particle0->Geometry()->IsUnderlyingUnion()))
			{
				Constraint.Particle[0] = Particle1;
				Constraint.Particle[1] = Particle0;
				Constraint.ImplicitTransform[0] = LocalTransform1;
				Constraint.ImplicitTransform[1] = LocalTransform0;
				Constraint.SetManifold(Implicit1, Implicit0);
			}
			else
			{
				Constraint.Particle[0] = Particle0;
				Constraint.Particle[1] = Particle1;
				Constraint.SetManifold(Implicit0, Implicit1);
			}

			if (T_TRAITS::bImmediateUpdate)
			{
				UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest>(CullDistance, Constraint);
				NewConstraints.TryAdd(CullDistance, Constraint);
			}
			else
			{
				NewConstraints.Add(Constraint);
			}
		}


		//
		// Constraint API
		//


		void UpdateManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal CullDistance, const FCollisionContext& Context)
		{
			const FImplicitObject& Implicit0 = *Constraint.Manifold.Implicit[0];
			const FImplicitObject& Implicit1 = *Constraint.Manifold.Implicit[1];
			const FRigidTransform3 WorldTransform0 = Constraint.ImplicitTransform[0] * ParticleTransform0;
			const FRigidTransform3 WorldTransform1 = Constraint.ImplicitTransform[1] * ParticleTransform1;

			switch (Constraint.Manifold.ShapesType)
			{
			case EContactShapesType::CapsuleBox:
				UpdateCapsuleBoxManifold(*Implicit0.template GetObject<const TCapsule<FReal>>(), WorldTransform0, Implicit1.template GetObject<const TBox<FReal, 3>>()->GetAABB(), WorldTransform1, CullDistance, Context, Constraint);
				break;
			case EContactShapesType::ConvexConvex:
				UpdateConvexConvexManifold(Constraint, WorldTransform0, WorldTransform1, CullDistance);
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
		template<ECollisionUpdateType UpdateType>
		inline void UpdateConstraintFromGeometryImpl(FRigidBodyPointContactConstraint& Constraint, const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal CullDistance)
		{
			const FImplicitObject& Implicit0 = *Constraint.Manifold.Implicit[0];
			const FImplicitObject& Implicit1 = *Constraint.Manifold.Implicit[1];

			switch (Constraint.Manifold.ShapesType)
			{
			case EContactShapesType::SphereSphere:
				UpdateSphereSphereConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<TSphere<FReal, 3>>(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SphereCapsule:
				UpdateSphereCapsuleConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<TCapsule<FReal>>(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SphereBox:
				UpdateSphereBoxConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, Implicit1.template GetObject<TBox<FReal, 3>>()->GetAABB(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SphereConvex:
				// MISSING CASE!!!
				UpdateConvexConvexConstraint(Implicit0, WorldTransform0, Implicit1, WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SphereTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateSphereTriangleMeshConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *ScaledTriMesh, WorldTransform1, CullDistance, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateSphereTriangleMeshConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *TriangleMeshImplicit, WorldTransform1, CullDistance, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::SphereHeightField:
				UpdateSphereHeightFieldConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject< FHeightField >(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::SpherePlane:
				UpdateSpherePlaneConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<TPlane<FReal, 3>>(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::CapsuleCapsule:
				UpdateCapsuleCapsuleConstraint(*Implicit0.template GetObject<TCapsule<FReal>>(), WorldTransform0, *Implicit1.template GetObject<TCapsule<FReal>>(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::CapsuleBox:
				UpdateCapsuleBoxConstraint(*Implicit0.template GetObject<TCapsule<FReal>>(), WorldTransform0, Implicit1.template GetObject<TBox<FReal, 3>>()->GetAABB(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::CapsuleConvex:
				// MISSING CASE!!!
				UpdateConvexConvexConstraint(Implicit0, WorldTransform0, Implicit1, WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::CapsuleTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateCapsuleTriangleMeshConstraint(*Implicit0.template GetObject<TCapsule<FReal>>(), WorldTransform0, *ScaledTriMesh, WorldTransform1, CullDistance, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateCapsuleTriangleMeshConstraint(*Implicit0.template GetObject<TCapsule<FReal>>(), WorldTransform0, *TriangleMeshImplicit, WorldTransform1, CullDistance, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::CapsuleHeightField:
				UpdateCapsuleHeightFieldConstraint(*Implicit0.template GetObject<TCapsule<FReal>>(), WorldTransform0, *Implicit1.template GetObject< FHeightField >(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::BoxBox:
				UpdateBoxBoxConstraint(Implicit0.template GetObject<TBox<FReal, 3>>()->GetAABB(), WorldTransform0, Implicit1.template GetObject<TBox<FReal, 3>>()->GetAABB(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::BoxConvex:
				// MISSING CASE!!!
				UpdateConvexConvexConstraint(Implicit0, WorldTransform0, Implicit1, WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::BoxTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateBoxTriangleMeshConstraint(Implicit0.template GetObject<TBox<FReal, 3>>()->GetAABB(), WorldTransform0, *ScaledTriMesh, WorldTransform1, CullDistance, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateBoxTriangleMeshConstraint(Implicit0.template GetObject<TBox<FReal, 3>>()->GetAABB(), WorldTransform0, *TriangleMeshImplicit, WorldTransform1, CullDistance, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::BoxHeightField:
				UpdateBoxHeightFieldConstraint(Implicit0.template GetObject<TBox<FReal, 3>>()->GetAABB(), WorldTransform0, *Implicit1.template GetObject< FHeightField >(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::BoxPlane:
				UpdateBoxPlaneConstraint(Implicit0.template GetObject<TBox<FReal, 3>>()->GetAABB(), WorldTransform0, *Implicit1.template GetObject<TPlane<FReal, 3>>(), WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::ConvexConvex:
				UpdateConvexConvexConstraint(Implicit0, WorldTransform0, Implicit1, WorldTransform1, CullDistance, Constraint);
				break;
			case EContactShapesType::ConvexTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateConvexTriangleMeshConstraint(Implicit0, WorldTransform0, *ScaledTriMesh, WorldTransform1, CullDistance, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateConvexTriangleMeshConstraint(Implicit0, WorldTransform0, *TriangleMeshImplicit, WorldTransform1, CullDistance, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::ConvexHeightField:
				UpdateConvexHeightFieldConstraint(Implicit0, WorldTransform0, *Implicit1.template GetObject< FHeightField >(), WorldTransform1, CullDistance, Constraint);
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

		template<typename T_TRAITS>
		void ConstructConstraintsImpl(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			// @todo(chaos): We use GetInnerType here because TriMeshes are left with their "Instanced" wrapper, unlike all other instanced implicits. Should we strip the instance on Tri Mesh too?
			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetCollisionType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetCollisionType()) : ImplicitObjectType::Unknown;
			bool bIsConvex0 = Implicit0 && Implicit0->IsConvex() && Implicit0Type != ImplicitObjectType::LevelSet;
			bool bIsConvex1 = Implicit1 && Implicit1->IsConvex() && Implicit1Type != ImplicitObjectType::LevelSet;

			FReal LengthCCD = 0.0f;
			FVec3 DirCCD(0.0f);
			bool bUseCCD = UseCCD(Particle0, Particle1, Implicit0, DirCCD, LengthCCD);

			if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxBoxConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				ConstructBoxHeightFieldConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FHeightField::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxHeightFieldConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TPlane<FReal, 3>::StaticType())
			{
				ConstructBoxPlaneConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TPlane<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxPlaneConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereSphereConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				ConstructSphereHeightFieldConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FHeightField::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereHeightFieldConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TPlane<FReal, 3>::StaticType())
			{
				ConstructSpherePlaneConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TPlane<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSpherePlaneConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructSphereBoxConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereBoxConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				ConstructSphereCapsuleConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereCapsuleConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				ConstructCapsuleCapsuleConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructCapsuleBoxConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				ConstructCapsuleBoxConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Context, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleHeightFieldConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleHeightFieldConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FHeightField::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleHeightFieldConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleHeightFieldConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructBoxTriangleMeshConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxTriangleMeshConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructSphereTriangleMeshConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereTriangleMeshConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleTriangleMeshConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TCapsule<FReal>::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleTriangleMeshConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (bIsConvex0 && Implicit1Type == FHeightField::StaticType())
			{
				if (bUseCCD)
				{
					ConstructConvexHeightFieldConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexHeightFieldConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FHeightField::StaticType() && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructConvexHeightFieldConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexHeightFieldConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (bIsConvex0 && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				if (bUseCCD)
				{
					ConstructConvexTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexTriangleMeshConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructConvexTriangleMeshConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexTriangleMeshConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, NewConstraints);
			}
			else if (bIsConvex0 && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructConvexConvexConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexConvexConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
			else
			{
				ConstructLevelsetLevelsetConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
			}
		}


		// Run collision detection for the specified constraint to update the nearest contact point.
		// NOTE: Transforms are world space particle transforms
		template<ECollisionUpdateType UpdateType>
		void UpdateConstraintFromGeometry(FRigidBodyPointContactConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal CullDistance)
		{
			const FRigidTransform3 WorldTransform0 = Constraint.ImplicitTransform[0] * ParticleTransform0;
			const FRigidTransform3 WorldTransform1 = Constraint.ImplicitTransform[1] * ParticleTransform1;
			UpdateConstraintFromGeometryImpl<UpdateType>(Constraint, WorldTransform0, WorldTransform1, CullDistance);
		}

		// Select the best manifold point as the new contact point.
		// NOTE: Transforms are world space particle transforms
		void UpdateConstraintFromManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal CullDistance)
		{
			const FRigidTransform3 WorldTransform0 = Constraint.ImplicitTransform[0] * ParticleTransform0;
			const FRigidTransform3 WorldTransform1 = Constraint.ImplicitTransform[1] * ParticleTransform1;
			const int32 NumPoints = Constraint.NumManifoldPoints();

			// Fall back to full collision detection if we have no manifold (or for testing)
			if ((NumPoints == 0) || bChaos_Collision_UseManifolds_Test)
			{
				UpdateConstraintFromGeometryImpl<ECollisionUpdateType::Deepest>(Constraint, WorldTransform0, WorldTransform1, CullDistance);
				return;
			}

			// Get the plane and point transforms (depends which body owns the plane)
			const FRigidTransform3& PlaneTransform = (Constraint.GetManifoldPlaneOwnerIndex() == 0) ? WorldTransform0 : WorldTransform1;
			const FRigidTransform3& PointsTransform = (Constraint.GetManifoldPlaneOwnerIndex() == 0) ? WorldTransform1 : WorldTransform0;

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
			if (Shape0 && (!Shape0->GetSimEnabled() || !IsValid(Shape0->GetSimData()) ) ) return false;
			if (Shape1 && (!Shape1->GetSimEnabled() || !IsValid(Shape1->GetSimData()) ) ) return false;

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


		template<typename T_TRAITS>
		void ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetType()) : ImplicitObjectType::Unknown;


			if (!Implicit0 || !Implicit1)
			{
				ConstructLevelsetLevelsetConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, NewConstraints);
				return;
			}

			EImplicitObjectType Implicit0OuterType = Implicit0->GetType();
			EImplicitObjectType Implicit1OuterType = Implicit1->GetType();

			// Handle transform wrapper shape
			if ((Implicit0OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType()) && (Implicit1OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType()))
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				FRigidTransform3 TransformedTransform0 = TransformedImplicit0->GetTransform() * LocalTransform0;
				FRigidTransform3 TransformedTransform1 = TransformedImplicit1->GetTransform() * LocalTransform1;
				ConstructConstraints<T_TRAITS>(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), TransformedImplicit1->GetTransformedObject(), TransformedTransform0, TransformedTransform1, CullDistance, Context, NewConstraints);
				return;
			}
			else if (Implicit0OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				FRigidTransform3 TransformedTransform0 = TransformedImplicit0->GetTransform() * LocalTransform0;
				ConstructConstraints<T_TRAITS>(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), Implicit1, TransformedTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				FRigidTransform3 TransformedTransform1 = TransformedImplicit1->GetTransform() * LocalTransform1;
				ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, TransformedImplicit1->GetTransformedObject(), LocalTransform0, TransformedTransform1, CullDistance, Context, NewConstraints);
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
					ConstructConstraints<T_TRAITS>(Particle0, Particle1, InnerImplicit0, InnerImplicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
					return;
				}
				else if (InnerImplicit0 && !InnerImplicit1)
				{
					ConstructConstraints<T_TRAITS>(Particle0, Particle1, InnerImplicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
					return;
				}
				else if (!InnerImplicit0 && InnerImplicit1)
				{
					ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, InnerImplicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
					return;
				}

			}

			// Handle Unions
			if (Implicit0OuterType == FImplicitObjectUnion::StaticType())
			{
				const FImplicitObjectUnion* Union0 = Implicit0->template GetObject<FImplicitObjectUnion>();
				for (const auto& Child0 : Union0->GetObjects())
				{
					ConstructConstraints<T_TRAITS>(Particle0, Particle1, Child0.Get(), Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
				}
				return;
			}

#if CHAOS_COLLIDE_CLUSTERED_UNIONS
			if(Implicit0OuterType == FImplicitObjectUnionClustered::StaticType())
			{
				const FImplicitObjectUnionClustered* Union0 = Implicit0->template GetObject<FImplicitObjectUnionClustered>();
				if(Implicit1->HasBoundingBox())
				{
					TArray<Pair<const FImplicitObject*, FRigidTransform3>> Children;

					// Need to get transformed bounds of 1 in the space of 0
					FRigidTransform3 TM0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 TM1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					FRigidTransform3 TM1ToTM0 = TM1.GetRelativeTransform(TM0);
					FAABB3 QueryBounds = Implicit1->BoundingBox().TransformedAABB(TM1ToTM0);

					Union0->FindAllIntersectingObjects(Children, QueryBounds);

					for(const Pair<const FImplicitObject*, FRigidTransform3>& Child0 : Children)
					{
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Child0.First, Implicit1, Child0.Second * LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
					}
				}
				else
				{
					for(const TUniquePtr<FImplicitObject>& Child0 : Union0->GetObjects())
					{
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Child0.Get(), Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
					}
				}
				return;
			}
#endif

			if (Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				const FImplicitObjectUnion* Union1 = Implicit1->template GetObject<FImplicitObjectUnion>();
				for (const auto& Child1 : Union1->GetObjects())
				{
					ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Child1.Get(), LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
				}
				return;
			}

#if CHAOS_COLLIDE_CLUSTERED_UNIONS
			if(Implicit1OuterType == FImplicitObjectUnionClustered::StaticType())
			{
				const FImplicitObjectUnionClustered* Union1 = Implicit1->template GetObject<FImplicitObjectUnionClustered>();
				if(Implicit0->HasBoundingBox())
				{
					TArray<Pair<const FImplicitObject*, FRigidTransform3>> Children;
					
					// Need to get transformed bounds of 0 in the space of 1
					FRigidTransform3 TM0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 TM1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					FRigidTransform3 TM0ToTM1 = TM0.GetRelativeTransform(TM1);
					FAABB3 QueryBounds = Implicit0->BoundingBox().TransformedAABB(TM0ToTM1);

					Union1->FindAllIntersectingObjects(Children, QueryBounds);

					for(const Pair<const FImplicitObject*, FRigidTransform3>& Child1 : Children)
					{
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Child1.First, LocalTransform0, Child1.Second * LocalTransform1, CullDistance, Context, NewConstraints);
					}
				}
				else
				{
					for(const TUniquePtr<FImplicitObject>& Child1 : Union1->GetObjects())
					{
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Child1.Get(), LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
					}
				}
				return;
			}
#endif

			// Check shape pair filtering if enable
			if (Context.bFilteringEnabled && !DoCollide(Implicit0Type, Particle0->GetImplicitShape(Implicit0), Implicit1Type, Particle1->GetImplicitShape(Implicit1)))
			{
				return;
			}

			// If we get here, we have a pair of concrete shapes (i.e., no wrappers or containers)
			// Create a constraint for the shape pair
			ConstructConstraintsImpl<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
		}

		void ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			if (Context.bDeferUpdate && !Context.bAllowManifolds)
			{
				using TTraits = TConstructCollisionTraits<false, false>;
				ConstructConstraints<TTraits>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
			}
			else if (Context.bDeferUpdate && Context.bAllowManifolds)
			{
				using TTraits = TConstructCollisionTraits<false, true>;
				ConstructConstraints<TTraits>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
			}
			else if (!Context.bDeferUpdate && Context.bAllowManifolds)
			{
				using TTraits = TConstructCollisionTraits<true, true>;
				ConstructConstraints<TTraits>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
			}
			else if (!Context.bDeferUpdate && !Context.bAllowManifolds)
			{
				using TTraits = TConstructCollisionTraits<true, false>;
				ConstructConstraints<TTraits>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Context, NewConstraints);
			}
		}

		template <typename GeometryA, typename GeometryB>
		bool GetPairTOIHackImpl(const FImplicitObject& A, const FRigidTransform3& AStartTransform, const GeometryB& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, FReal& OutTOI, FVec3& OutNormal, FReal& OutPhi)
		{
			FReal CullDistance = 0.0f;

			FReal TOI = 1.0f;
			FContactPoint Contact = GJKImplicitSweptContactPoint<GeometryA>(A, AStartTransform, B, BTransform, Dir, Length, CullDistance, TOI);
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
			FContactPoint Contact;
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

			FContactPoint Contact = ConvexConvexContactPointSwept(A, AStartTransform, B, BTransform, Dir, Length, CullDistance, TOI);
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

			if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				return GetPairTOIHackImpl<TBox<FReal, 3>>(*Implicit0->template GetObject<TBox<FReal, 3>>(), StartTransform0, *Implicit1->template GetObject<FHeightField>(), Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				return GetPairTOIHackImpl<TSphere<FReal, 3>>(*Implicit0->template GetObject<TSphere<FReal, 3>>(), StartTransform0, *Implicit1->template GetObject<FHeightField>(), Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
			}
			else if (Implicit0Type == TCapsule<FReal>::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				return GetPairTOIHackImpl<TCapsule<FReal>>(*Implicit0->template GetObject<TCapsule<FReal>>(), StartTransform0, *Implicit1->template GetObject<FHeightField>(), Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
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
			else if (bIsConvex0 && Implicit1Type == FHeightField::StaticType())
			{
				return GetPairTOIHackImpl<FConvex>(*Implicit0, StartTransform0, *Implicit1->template GetObject<FHeightField>(), Transform1, Dir, Length, OutTOI, OutNormal, OutPhi);
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

		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Any>(const float CullDistance, FRigidBodyPointContactConstraint& Constraint);
		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest>(const float CullDistance, FRigidBodyPointContactConstraint& Constraint);

		template void UpdateConstraintFromGeometry<ECollisionUpdateType::Any>(FRigidBodyPointContactConstraint& ConstraintBase, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const float CullDistance);
		template void UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest>(FRigidBodyPointContactConstraint& ConstraintBase, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const float CullDistance);

	} // Collisions

} // Chaos
