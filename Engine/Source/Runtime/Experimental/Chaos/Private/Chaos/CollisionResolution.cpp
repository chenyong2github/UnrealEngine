// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolution.h"

#include "Chaos/CastingUtilities.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Capsule.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Collision/CapsuleConvexContactPoint.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Collision/SphereConvexContactPoint.h"
#include "Chaos/Convex.h"
#include "Chaos/Defines.h"
#include "Chaos/HeightField.h"
#include "Chaos/ImplicitFwd.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/Sphere.h"
#include "Chaos/Transform.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/CollisionOneShotManifolds.h"

//PRAGMA_DISABLE_OPTIMIZATION

#if 0
DECLARE_CYCLE_STAT(TEXT("Collisions::GJK"), STAT_Collisions_GJK, STATGROUP_ChaosCollision);
#define SCOPE_CYCLE_COUNTER_GJK() SCOPE_CYCLE_COUNTER(STAT_Collisions_GJK)
#else
#define SCOPE_CYCLE_COUNTER_GJK()
#endif

// @todo(chaos): clean up the contact creation time rejection to avoid extra transforms
#define CHAOS_COLLISION_CREATE_BOUNDSCHECK 1

DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructConstraints"), STAT_Collisions_ConstructConstraints, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructConstraintsInternal"), STAT_Collisions_ConstructConstraintsInternal, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::FindAllIntersectingClusteredObjects"), STAT_Collisions_FindAllIntersectingClusteredObjects, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructGenericConvexConvexConstraints"), STAT_Collisions_ConstructGenericConvexConvexConstraints, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructGenericConvexConvexConstraintsSwept"), STAT_Collisions_ConstructGenericConvexConvexConstraintsSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::SetSweptConstraintTOI"), STAT_Collisions_SetSweptConstraintTOI, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateConstraintFromGeometryInternal"), STAT_Collisions_UpdateConstraintFromGeometryInternal, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateGenericConvexConvexConstraint"), STAT_Collisions_UpdateGenericConvexConvexConstraint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateConvexTriangleMeshConstraint"), STAT_Collisions_UpdateConvexTriangleMeshConstraint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateConvexTriangleMeshConstraintSwept"), STAT_Collisions_UpdateConvexTriangleMeshConstraintSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConvexHeightFieldContactPoint"), STAT_Collisions_ConvexHeightFieldContactPoint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConvexTriangleMeshContactPoint"), STAT_Collisions_ConvexTriangleMeshContactPoint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConvexTriangleMeshSweptContactPoint"), STAT_Collisions_ConvexTriangleMeshSweptContactPoint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateConvexHeightFieldConstraint"), STAT_Collisions_UpdateConvexHeightFieldConstraint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructConvexHeightFieldConstraints"), STAT_Collisions_ConstructConvexHeightFieldConstraints, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructConvexHeightFieldConstraintsSwept"), STAT_Collisions_ConstructConvexHeightFieldConstraintsSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructConvexTriangleMeshConstraints"), STAT_Collisions_ConstructConvexTriangleMeshConstraints, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructConvexTriangleMeshConstraintsSwept"), STAT_Collisions_ConstructConvexTriangleMeshConstraintsSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructCapsuleTriangleMeshConstraintsSwept"), STAT_Collisions_ConstructCapsuleTriangleMeshConstraintsSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructCapsuleTriangleMeshConstraints"), STAT_Collisions_ConstructCapsuleTriangleMeshConstraints, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructCapsuleHeightFieldConstraints"), STAT_Collisions_ConstructCapsuleHeightFieldConstraints, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::ConstructCapsuleHeightFieldConstraintsSwept"), STAT_Collisions_ConstructCapsuleHeightFieldConstraintsSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::CapsuleTriangleMeshContactPoint"), STAT_Collisions_CapsuleTriangleMeshContactPoint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::CapsuleTriangleMeshSweptContactPoint"), STAT_Collisions_CapsuleTriangleMeshSweptContactPoint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::CapsuleHeightFieldContactPoint"), STAT_Collisions_CapsuleHeightFieldContactPoint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateGenericConvexConvexConstraintSwept"), STAT_Collisions_UpdateGenericConvexConvexConstraintSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateCapsuleHeightFieldConstraintSwept"), STAT_Collisions_UpdateCapsuleHeightFieldConstraintSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateCapsuleHeightFieldConstraint"), STAT_Collisions_UpdateCapsuleHeightFieldConstraint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateCapsuleTriangleMeshConstraint"), STAT_Collisions_UpdateCapsuleTriangleMeshConstraint, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateCapsuleTriangleMeshConstraintSwept"), STAT_Collisions_UpdateCapsuleTriangleMeshConstraintSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateConvexHeightFieldConstraintSwept"), STAT_Collisions_UpdateConvexHeightFieldConstraintSwept, STATGROUP_ChaosCollision);
DECLARE_CYCLE_STAT(TEXT("Collisions::UpdateLevelsetLevelsetConstraint"), STAT_UpdateLevelsetLevelsetConstraint, STATGROUP_ChaosCollision);




Chaos::FRealSingle CCDEnableThresholdBoundsScale = 0.4f;
FAutoConsoleVariableRef  CVarCCDEnableThresholdBoundsScale(TEXT("p.Chaos.CCD.EnableThresholdBoundsScale"), CCDEnableThresholdBoundsScale , TEXT("CCD is used when object position is changing > smallest bound's extent * BoundsScale. 0 will always Use CCD. Values < 0 disables CCD."));

Chaos::FRealSingle CCDAllowedDepthBoundsScale = 0.1f;
FAutoConsoleVariableRef CVarCCDAllowedDepthBoundsScale(TEXT("p.Chaos.CCD.AllowedDepthBoundsScale"), CCDAllowedDepthBoundsScale, TEXT("When rolling back to TOI, allow (smallest bound's extent) * AllowedDepthBoundsScale, instead of rolling back to exact TOI w/ penetration = 0."));

int32 ConstraintsDetailedStats = 0;
FAutoConsoleVariableRef CVarConstraintsDetailedStats(TEXT("p.Chaos.Constraints.DetailedStats"), ConstraintsDetailedStats, TEXT("When set to 1, will enable more detailed stats."));

int32 AlwaysAddSweptConstraints = 0;
FAutoConsoleVariableRef CVarAlwaysAddSweptConstraints(TEXT("p.Chaos.Constraints.AlwaysAddSweptConstraints"), AlwaysAddSweptConstraints, TEXT("Since GJKContactPointSwept returns infinity for it's contact data when not hitting anything, some contacts are discarded prematurely. This flag will cause contact points considered for sweeps to never be discarded."));

//Chaos::FRealSingle Chaos_Collision_ManifoldFaceAngle = 5.0f;
//Chaos::FRealSingle Chaos_Collision_ManifoldFaceEpsilon = FMath::Sin(FMath::DegreesToRadians(Chaos_Collision_ManifoldFaceAngle));
//FConsoleVariableDelegate Chaos_Collision_ManifoldFaceDelegate = FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* CVar) { Chaos_Collision_ManifoldFaceEpsilon = FMath::Sin(FMath::DegreesToRadians(Chaos_Collision_ManifoldFaceAngle)); });
//FAutoConsoleVariableRef CVarChaosCollisionManifoldFaceAngle(TEXT("p.Chaos.Collision.ManifoldFaceAngle"), Chaos_Collision_ManifoldFaceAngle, TEXT("Angle above which a face is rejected and we switch to point collision"), Chaos_Collision_ManifoldFaceDelegate);
//
//Chaos::FRealSingle Chaos_Collision_ManifoldPositionTolerance = 0.5f;
//Chaos::FRealSingle Chaos_Collision_ManifoldRotationTolerance = 0.05f;
//bool bChaos_Collision_ManifoldToleranceExceededRebuild = true;
//FAutoConsoleVariableRef CVarChaosCollisionManifoldPositionTolerance(TEXT("p.Chaos.Collision.ManifoldPositionTolerance"), Chaos_Collision_ManifoldPositionTolerance, TEXT(""));
//FAutoConsoleVariableRef CVarChaosCollisionManifoldRotationTolerance(TEXT("p.Chaos.Collision.ManifoldRotationTolerance"), Chaos_Collision_ManifoldRotationTolerance, TEXT(""));
//FAutoConsoleVariableRef CVarChaosCollisionManifoldToleranceExceededRebuild(TEXT("p.Chaos.Collision.ManifoldToleranceRebuild"), bChaos_Collision_ManifoldToleranceExceededRebuild, TEXT(""));

bool Chaos_Collision_NarrowPhase_SphereBoundsCheck = true;
bool Chaos_Collision_NarrowPhase_AABBBoundsCheck = true;
FAutoConsoleVariableRef CVarChaosCollisionSphereBoundsCheck(TEXT("p.Chaos.Collision.SphereBoundsCheck"), Chaos_Collision_NarrowPhase_SphereBoundsCheck, TEXT(""));
FAutoConsoleVariableRef CVarChaosCollisionAABBBoundsCheck(TEXT("p.Chaos.Collision.AABBBoundsCheck"), Chaos_Collision_NarrowPhase_AABBBoundsCheck, TEXT(""));


namespace Chaos
{
	namespace Collisions
	{

		// Traits to control how contacts are generated
		template<bool B_IMMEDIATEUPDATE>
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
		};

		// GJKPenetration (EPA) can return arbitrarily wrong normals when Phi approaches zero (it ends up renormalizing
		// a very small vector) so attempt to fix the normal by calling PhiWithNormal.
		template<typename T_SHAPE>
		FORCEINLINE void FixGJKPenetrationNormal(FContactPoint& ContactPoint, const T_SHAPE& Shape1, const FTransform& WorldTransform1)
		{
			FVec3 NormalLocal1;
			const FReal Phi = Shape1.PhiWithNormal(ContactPoint.ShapeContactPoints[1], NormalLocal1);

			ContactPoint.ShapeContactNormal = NormalLocal1;
			ContactPoint.Normal = WorldTransform1.TransformVectorNoScale(NormalLocal1);
			ContactPoint.Phi = Phi;
		}

		// Determines if body should use CCD. If using CCD, computes Dir and Length of sweep.
		bool UseCCD(const TGeometryParticleHandle<FReal, 3>* SweptParticle, const TGeometryParticleHandle<FReal, 3>* OtherParticle, const FImplicitObject* Implicit, FVec3& Dir, FReal& Length)
		{
			if (OtherParticle->ObjectState() != EObjectStateType::Static)
			{
				return false;
			}

			auto* RigidParticle = SweptParticle->CastToRigidParticle();
			if (RigidParticle  && RigidParticle->CCDEnabled())
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
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_SetSweptConstraintTOI, ConstraintsDetailedStats);

			if (SweptConstraint.Manifold.Phi > 0.0f)
			{
				SweptConstraint.TimeOfImpact = 1.f;
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

		// Add the contact to the manifold (or update the existing point). Disable the contact if the contact distance is greater than the Cull Distance
		void UpdateContactPoint(FRigidBodyPointContactConstraint& Constraint, const FContactPoint& ContactPoint, const FReal Dt)
		{
			// Permanently disable contacts beyond the CullDistance
#if CHAOS_COLLISION_CREATE_BOUNDSCHECK
			if (ContactPoint.Phi > Constraint.GetCullDistance())
			{
				Constraint.SetDisabled(true);
				return;
			}
#endif

			// Ignore points that have not been initialized - i.e., if there is no detectable contact 
			// point within reasonable range despite passing the AABB tests
			if (ContactPoint.IsSet())
			{
				Constraint.AddIncrementalManifoldContact(ContactPoint, Dt);
			}
		}

		// Same as UpdateContact Point but without checking CullDistance. Used by CCD because sweeps do not set the separation unless the sweep actually hits
		void UpdateContactPointNoCull(FRigidBodyPointContactConstraint& Constraint, const FContactPoint& ContactPoint, const FReal Dt)
		{
			if (ContactPoint.IsSet())
			{
				Constraint.AddIncrementalManifoldContact(ContactPoint, Dt);
			}
		}


		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKContactPoint2(const GeometryA& A, const GeometryB& B, const FRigidTransform3& ATM, const FRigidTransform3& BToATM, const FVec3& InitialDir, const FReal ShapePadding)
		{
			SCOPE_CYCLE_COUNTER_GJK();

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
				Contact.ShapeMargins[0] = 0.0f;
				Contact.ShapeMargins[1] = 0.0f;
				Contact.ShapeContactPoints[0] = ClosestA;
				Contact.ShapeContactPoints[1] = BToATM.InverseTransformPosition(ClosestBInA);
				Contact.ShapeContactNormal = -BToATM.InverseTransformVector(Normal);
				Contact.Location = ATM.TransformPosition(ClosestA + ThicknessA * Normal);
				Contact.Normal = -ATM.TransformVectorNoScale(Normal);
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

		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKContactPointSwept(const GeometryA& A, const FRigidTransform3& ATM, const GeometryB& B, const FRigidTransform3& BTM, const FVec3& Dir, const FReal Length, FReal& TOI)
		{
			FContactPoint Contact;
			const FRigidTransform3 AToBTM = ATM.GetRelativeTransform(BTM);
			const FVec3 LocalDir = BTM.InverseTransformVectorNoScale(Dir);

			FReal OutTime;
			FVec3 Location, Normal;
			if (GJKRaycast2(B, A, AToBTM, LocalDir, Length, OutTime, Location, Normal, (FReal)0, true))
			{
				// GJK output is all in the local space of B. We need to transform the B-relative position and the normal in to B-space
				Contact.ShapeMargins[0] = 0.0f;
				Contact.ShapeMargins[1] = 0.0f;
				Contact.ShapeContactPoints[0] = AToBTM.InverseTransformPosition(Location);
				Contact.ShapeContactPoints[1] = Location;
				Contact.ShapeContactNormal = -Normal;
				Contact.Location = BTM.TransformPosition(Location);
				Contact.Normal = BTM.TransformVectorNoScale(Normal);
				ComputeSweptContactPhiAndTOIHelper(Contact.Normal, Dir, Length, OutTime, TOI, Contact.Phi);
			}

			return Contact;
		}


		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKImplicitContactPoint(const FImplicitObject& A, const FRigidTransform3& ATransform, const GeometryB& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			FContactPoint Contact;
			const FRigidTransform3 AToBTM = ATransform.GetRelativeTransform(BTransform);

			FReal ContactPhi = FLT_MAX;
			FVec3 Location, Normal;
			if (const TImplicitObjectScaled<GeometryA>* ScaledConvexImplicit = A.template GetObject<const TImplicitObjectScaled<GeometryA> >())
			{
				if (B.GJKContactPoint(*ScaledConvexImplicit, AToBTM, CullDistance, Location, Normal, ContactPhi))
				{
					// @todo(chaos): margin
					Contact.ShapeMargins[0] = 0.0f;
					Contact.ShapeMargins[1] = 0.0f;
					Contact.ShapeContactPoints[0] = AToBTM.InverseTransformPosition(Location);
					Contact.ShapeContactPoints[1] = Location - ContactPhi * Normal;
					Contact.ShapeContactNormal = Normal;
					Contact.Phi = ContactPhi;
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVectorNoScale(Normal);
				}
			}
			else if (const TImplicitObjectInstanced<GeometryA>* InstancedConvexImplicit = A.template GetObject<const TImplicitObjectInstanced<GeometryA> >())
			{
				if (const GeometryA* InstancedInnerObject = static_cast<const GeometryA*>(InstancedConvexImplicit->GetInstancedObject()))
				{
					if (B.GJKContactPoint(*InstancedInnerObject, AToBTM, CullDistance, Location, Normal, ContactPhi))
					{
						// @todo(chaos): margin
						Contact.ShapeMargins[0] = 0.0f;
						Contact.ShapeMargins[1] = 0.0f;
						Contact.ShapeContactPoints[0] = AToBTM.InverseTransformPosition(Location);
						Contact.ShapeContactPoints[1] = Location - ContactPhi * Normal;
						Contact.ShapeContactNormal = Normal;
						Contact.Phi = ContactPhi;
						Contact.Location = BTransform.TransformPosition(Location);
						Contact.Normal = BTransform.TransformVectorNoScale(Normal);
					}
				}
			}
			else if (const GeometryA* ConvexImplicit = A.template GetObject<const GeometryA>())
			{
				if (B.GJKContactPoint(*ConvexImplicit, AToBTM, CullDistance, Location, Normal, ContactPhi))
				{
					// @todo(chaos): margin
					Contact.ShapeMargins[0] = 0.0f;
					Contact.ShapeMargins[1] = 0.0f;
					Contact.ShapeContactPoints[0] = AToBTM.InverseTransformPosition(Location);
					Contact.ShapeContactPoints[1] = Location - ContactPhi * Normal;
					Contact.ShapeContactNormal = Normal;
					Contact.Phi = ContactPhi;
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVectorNoScale(Normal);
				}
			}

			return Contact;
		}

		template <typename GeometryA, typename GeometryB>
		FContactPoint GJKImplicitSweptContactPoint(const FImplicitObject& A, const FRigidTransform3& AStartTransform, const GeometryB& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, FReal& TOI)
		{
			FContactPoint Contact;
			const FRigidTransform3 AToBTM = AStartTransform.GetRelativeTransform(BTransform);
			const FVec3 LocalDir = BTransform.InverseTransformVectorNoScale(Dir);

			FReal OutTime = FLT_MAX;
			int32 FaceIndex = -1;
			FVec3 Location, Normal;

			Utilities::CastHelper(A, AStartTransform, [&](const auto& ADowncast, const FRigidTransform3& AFullTM)
			{
				// @todo(chaos): handle instances with margin
				if (B.SweepGeom(ADowncast, AToBTM, LocalDir, Length, OutTime, Location, Normal, FaceIndex, 0.0f, true))
				{
					// @todo(chaos): margin
					Contact.ShapeMargins[0] = 0.0f;
					Contact.ShapeMargins[1] = 0.0f;
					Contact.ShapeContactPoints[0] = AToBTM.InverseTransformPosition(Location);
					Contact.ShapeContactPoints[1] = Location;
					Contact.ShapeContactNormal = Normal;
					Contact.Location = BTransform.TransformPosition(Location);
					Contact.Normal = BTransform.TransformVectorNoScale(Normal);
					ComputeSweptContactPhiAndTOIHelper(Contact.Normal, Dir, Length, OutTime, TOI, Contact.Phi);
				}
			});

			return Contact;
		}

		// A is the implicit here, we want to return a contact point on B (trimesh)
		template <typename GeometryA>
		FContactPoint GJKImplicitScaledTriMeshSweptContactPoint(const FImplicitObject& A, const FRigidTransform3& AStartTransform, const TImplicitObjectScaled<FTriangleMeshImplicitObject>& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, FReal& TOI)
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
				// @todo(chaos): handle Instanced with margin
				if (B.LowLevelSweepGeom(ADowncast, AToBTM, LocalDir, Length, OutTime, Location, Normal, FaceIndex, 0.0f, true))
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
			else if (Implicit0OuterType == TImplicitObjectInstanced<FCapsule>::StaticType())
			{
				return Implicit0->template GetObject<const TImplicitObjectInstanced<FCapsule>>()->GetInstancedObject();
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


		template<typename T_IMPLICITA>
		struct TConvexImplicitTraits
		{
			static const bool bSupportsOneShotManifold = false;
		};

		template<>
		struct TConvexImplicitTraits<FImplicitBox3>
		{
			static const bool bSupportsOneShotManifold = true;
		};
		template<>
		struct TConvexImplicitTraits<FImplicitConvex3>
		{
			static const bool bSupportsOneShotManifold = true;
		};
		template<>
		struct TConvexImplicitTraits<TImplicitObjectInstanced<FImplicitConvex3>>
		{
			static const bool bSupportsOneShotManifold = true;
		};
		template<>
		struct TConvexImplicitTraits<TImplicitObjectScaled<FImplicitConvex3>>
		{
			static const bool bSupportsOneShotManifold = true;
		};

		// Convex pair type traits
		template<typename T_IMPLICITA, typename T_IMPLICITB>
		struct TConvexImplicitPairTraits
		{
			// Whether the pair types should use a manifold
			static const bool bSupportsOneShotManifold = TConvexImplicitTraits<T_IMPLICITA>::bSupportsOneShotManifold && TConvexImplicitTraits<T_IMPLICITB>::bSupportsOneShotManifold;
		};

		// Use the traits to call the appropriate convex-convex update method. 
		// Either incremental manifold (default) or one-shot manifold
		template<bool T_SUPPORTSONESHOTMANIFOLD = false>
		struct TConvexConvexUpdater
		{
			template<typename T_ImplicitA, typename T_ImplicitB>
			static void UpdateConvexConvexConstraint(const T_ImplicitA& A, const FRigidTransform3& ATM, const T_ImplicitB& B, const FRigidTransform3& BTM, const FReal Dt, const FReal ShapePadding, FRigidBodyPointContactConstraint& Constraint)
			{
				UpdateContactPoint(Constraint, GJKContactPoint(A, ATM, B, BTM, FVec3(1, 0, 0), ShapePadding), Dt);
			}
		};

		// One-shot manifold convex-convex update
		template<>
		struct TConvexConvexUpdater<true>
		{
			template<typename T_ImplicitA, typename T_ImplicitB>
			static void UpdateConvexConvexConstraint(const T_ImplicitA& A, const FRigidTransform3& ATM, const T_ImplicitB& B, const FRigidTransform3& BTM, const FReal Dt, const FReal ShapePadding, FRigidBodyPointContactConstraint& Constraint)
			{
				if (Constraint.GetUseManifold())
				{
					// We only build one shot manifolds once, and from then on we just find the deepest point
					if (Constraint.GetManifoldPoints().Num() == 0)
					{
						ConstructConvexConvexOneShotManifold(A, ATM, B, BTM, Dt, Constraint);
					}
					else
					{
						Constraint.UpdateManifoldContacts(Dt);
					}
				}
				else
				{
					UpdateContactPoint(Constraint, GJKContactPoint(A, ATM, B, BTM, FVec3(1, 0, 0), ShapePadding), Dt);
				}
			}
		};

		// Another helper required by UpdateGenericConvexConvexConstraintHelper which uses CastHelper and does not have any typedefs 
		// for the concrete implicit types, so we need to rely on type deduction from the compiler.
		struct FConvexConvexUpdaterCaller
		{
			template<typename T_ImplicitA, typename T_ImplicitB>
			static void Update(const T_ImplicitA& A, const FRigidTransform3& ATM, const T_ImplicitB& B, const FRigidTransform3& BTM, const FReal Dt, const FReal ShapePadding, FRigidBodyPointContactConstraint& Constraint)
			{
				using FConvexConvexUpdater = TConvexConvexUpdater<TConvexImplicitPairTraits<T_ImplicitA, T_ImplicitB>::bSupportsOneShotManifold>;
				FConvexConvexUpdater::UpdateConvexConvexConstraint(A, ATM, B, BTM, Dt, ShapePadding, Constraint);
			}
		};


		// Unwrap the many convex types, including scaled, and call the appropriate update which depends on the concrete types
		void UpdateGenericConvexConvexConstraintHelper(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FReal Dt, const FReal ShapePadding, FRigidBodyPointContactConstraint& Constraint)
		{
			// This expands to a switch of switches that calls the inner function with the appropriate concrete implicit types
			Utilities::CastHelperNoUnwrap(A, ATM, [&](const auto& ADowncast, const FRigidTransform3& AFullTM)
			{
				Utilities::CastHelperNoUnwrap(B, BTM, [&](const auto& BDowncast, const FRigidTransform3& BFullTM)
				{
					FConvexConvexUpdaterCaller::Update(ADowncast, AFullTM, BDowncast, BFullTM, Dt, ShapePadding, Constraint);
				});
			});
		}

		FContactPoint GenericConvexConvexContactPoint(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FReal ShapePadding)
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

		FContactPoint GenericConvexConvexContactPointSwept(const FImplicitObject& A, const FRigidTransform3& ATM, const FImplicitObject& B, const FRigidTransform3& BTM, const FVec3& Dir, const FReal Length, FReal& TOI)
		{
			// This expands to a switch of switches that calls the inner function with the appropriate concrete implicit types
			return Utilities::CastHelperNoUnwrap(A, ATM, [&](const auto& ADowncast, const FRigidTransform3& AFullTM)
			{
				return Utilities::CastHelperNoUnwrap(B, BTM, [&](const auto& BDowncast, const FRigidTransform3& BFullTM)
				{
					return GJKContactPointSwept(ADowncast, AFullTM, BDowncast, BFullTM, Dir, Length, TOI);
				});
			});
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

		FContactPoint BoxBoxContactPoint(const FImplicitBox3& Box1, const FImplicitBox3& Box2, const FRigidTransform3& Box1TM, const FRigidTransform3& Box2TM, const FReal ShapePadding)
		{
			return GJKContactPoint(Box1, Box1TM, Box2, Box2TM, FVec3(1, 0, 0), ShapePadding);
		}

		void UpdateBoxBoxConstraint(const FImplicitBox3& Box1, const FRigidTransform3& Box1Transform, const FImplicitBox3& Box2, const FRigidTransform3& Box2Transform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			if (Constraint.GetUseManifold())
			{
				// We only build one shot manifolds once, and from then on we just find the deepest point
				if (Constraint.GetManifoldPoints().Num() == 0)
				{
					ConstructBoxBoxOneShotManifold(Box1, Box1Transform, Box2, Box2Transform, Dt, Constraint);
				}
				else
				{
					Constraint.UpdateManifoldContacts(Dt);
				}
			}
			else
			{
				UpdateContactPoint(Constraint, BoxBoxContactPoint(Box1, Box2, Box1Transform, Box2Transform, Constraint.Manifold.RestitutionPadding), Dt);
			}
		}

		template<typename T_TRAITS>
		void ConstructBoxBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const TBox<FReal, 3>* Object0 = Implicit0->template GetObject<const TBox<FReal, 3> >();
			const TBox<FReal, 3>* Object1 = Implicit1->template GetObject<const TBox<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::BoxBox, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateBoxBoxConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}


		//
		// Box - HeightField
		//

		FContactPoint BoxHeightFieldContactPoint(const FImplicitBox3& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			return GJKImplicitContactPoint<FImplicitBox3>(A, ATransform, B, BTransform, CullDistance, ShapePadding);
		}


		void UpdateBoxHeightFieldConstraint(const FImplicitBox3& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(chaos): restitutionpadding
			UpdateContactPoint(Constraint, BoxHeightFieldContactPoint(A, ATransform, B, BTransform, Constraint.GetCullDistance(), 0.0f), Dt);
		}

		template<typename T_TRAITS>
		void ConstructBoxHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{

			const TBox<FReal, 3>* Object0 = Implicit0->template GetObject<const TBox<FReal, 3> >();
			const FHeightField* Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Object0 && Object1))
			{
				// @todo(chaos): one-shot manifold
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::BoxHeightField, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateBoxHeightFieldConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}



		//
		// Box-Plane
		//

		FContactPoint BoxPlaneContactPoint(const FImplicitBox3& Box, const FImplicitPlane3& Plane, const FRigidTransform3& BoxTransform, const FRigidTransform3& PlaneTransform, const FReal ShapePadding)
		{
			FContactPoint Contact;

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

			Contact.ShapeMargins[0] = 0.0f;
			Contact.ShapeMargins[1] = 0.0f;
			Contact.ShapeContactPoints[0] = BoxTransform.InverseTransformPosition(Contact.Location);
			Contact.ShapeContactPoints[1] = PlaneTransform.InverseTransformPosition(Contact.Location - Contact.Phi * Contact.Normal);
			Contact.ShapeContactNormal = PlaneTransform.InverseTransformVector(Contact.Normal);

			return Contact;
		}

		void UpdateBoxPlaneConstraint(const FImplicitBox3& A, const FRigidTransform3& ATransform, const TPlane<FReal, 3>& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(chaos): one-shot manifold
			// @todo(chaos): restitutionpadding
			UpdateContactPoint(Constraint, BoxPlaneContactPoint(A, B, ATransform, BTransform, 0.0f), Dt);
		}


		template<typename T_TRAITS>
		void ConstructBoxPlaneConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const TBox<FReal, 3>* Object0 = Implicit0->template GetObject<const TBox<FReal, 3> >();
			const TPlane<FReal, 3>* Object1 = Implicit1->template GetObject<const TPlane<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::BoxPlane, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateBoxPlaneConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		//
		// Box-TriangleMesh
		//

		template <typename TriMeshType>
		FContactPoint BoxTriangleMeshContactPoint(const FImplicitBox3& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			return GJKImplicitContactPoint<TBox<FReal, 3>>(A, ATransform, B, BTransform, CullDistance, ShapePadding);
		}

		template <typename TriMeshType>
		void UpdateBoxTriangleMeshConstraint(const FImplicitBox3& Box0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @toto(chaos): restitutionpadding
			UpdateContactPoint(Constraint, BoxTriangleMeshContactPoint(Box0, WorldTransform0, TriangleMesh1, WorldTransform1, Constraint.GetCullDistance(), 0.0f), Dt);
		}

		template<typename T_TRAITS>
		void ConstructBoxTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const TBox<FReal, 3>* Object0 = Implicit0->template GetObject<const TBox<FReal, 3> >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					// @todo(chaos): one-shot manifold
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::BoxTriMesh, Context.bAllowManifolds);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateBoxTriangleMeshConstraint(*Object0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, Dt, Constraint);
					}
					NewConstraints.Add(Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					// @todo(chaos): one-shot manifold
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::BoxTriMesh, Context.bAllowManifolds);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateBoxTriangleMeshConstraint(*Object0, WorldTransform0, *TriangleMesh, WorldTransform1, Dt, Constraint);
					}
					NewConstraints.Add(Constraint);
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


		FContactPoint SphereSphereContactPoint(const TSphere<FReal, 3>& Sphere1, const FRigidTransform3& Sphere1Transform, const TSphere<FReal, 3>& Sphere2, const FRigidTransform3& Sphere2Transform, const FReal ShapePadding)
		{
			FContactPoint Result;

			const FReal R1 = Sphere1.GetRadius() + 0.5f * ShapePadding;
			const FReal R2 = Sphere2.GetRadius() + 0.5f * ShapePadding;

			// World-space contact
			const FVec3 Center1 = Sphere1Transform.TransformPosition(Sphere1.GetCenter());
			const FVec3 Center2 = Sphere2Transform.TransformPosition(Sphere2.GetCenter());
			const FVec3 Direction = Center1 - Center2;
			const FReal Size = Direction.Size();
			const FVec3 Normal = Size > SMALL_NUMBER ? Direction / Size : FVec3(0, 0, 1);
			const FReal NewPhi = Size - (R1 + R2);

			// @todo(chaos): margin
			Result.ShapeMargins[0] = 0.0f;
			Result.ShapeMargins[1] = 0.0f;
			Result.ShapeContactPoints[0] = Sphere1.GetCenter() - Sphere1Transform.InverseTransformVector(R1 * Normal);
			Result.ShapeContactPoints[1] = Sphere2.GetCenter() + Sphere2Transform.InverseTransformVector(R2 * Normal);
			Result.ShapeContactNormal = Sphere2Transform.InverseTransformVector(Normal);

			Result.Phi = NewPhi;
			Result.Normal = Normal;
			Result.Location = Center1 - R1 * Result.Normal;

			return Result;
		}

		void UpdateSphereSphereConstraint(const TSphere<FReal, 3>& Sphere1, const FRigidTransform3& Sphere1Transform, const TSphere<FReal, 3>& Sphere2, const FRigidTransform3& Sphere2Transform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint, SphereSphereContactPoint(Sphere1, Sphere1Transform, Sphere2, Sphere2Transform, Constraint.Manifold.RestitutionPadding), Dt);
		}

		template<typename T_TRAITS>
		void ConstructSphereSphereConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const TSphere<FReal, 3>* Object1 = Implicit1->template GetObject<const TSphere<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereSphere, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereSphereConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		//
		// Sphere - HeightField
		//

		FContactPoint SphereHeightFieldContactPoint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			return GJKImplicitContactPoint<TSphere<FReal, 3>>(TSphere<FReal, 3>(A), ATransform, B, BTransform, CullDistance, ShapePadding);
		}


		void UpdateSphereHeightFieldConstraint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(chaos): restitutionpadding
			UpdateContactPoint(Constraint, SphereHeightFieldContactPoint(A, ATransform, B, BTransform, Constraint.GetCullDistance(), 0.0f), Dt);
		}

		void UpdateSphereHeightFieldConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, const FReal Dt, FRigidBodySweptPointContactConstraint& Constraint)
		{
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint, GJKImplicitSweptContactPoint<TSphere<FReal, 3>>(A, ATransform, B, BTransform, Dir, Length, TOI), Dt);
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template<typename T_TRAITS>
		void ConstructSphereHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const FHeightField* Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereHeightField, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereHeightFieldConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		void ConstructSphereHeightFieldConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3>>();
			const FHeightField* Object1 = Implicit1->template GetObject<const FHeightField>();
			if (ensure(Object0 && Object1))
			{
				FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereHeightField);
				FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
				FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
				UpdateSphereHeightFieldConstraintSwept(Particle0, *Object0, WorldTransformX0, *Object1, WorldTransform1, Dir, Length, Dt, Constraint);
				NewConstraints.Add(Constraint);
			}
		}

		//
		//  Sphere-Plane
		//

		FContactPoint SpherePlaneContactPoint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const TPlane<FReal, 3>& Plane, const FRigidTransform3& PlaneTransform, const FReal ShapePadding)
		{
			FContactPoint Result;

			FReal SphereRadius = Sphere.GetRadius() + 0.5f * ShapePadding;

			FVec3 SpherePosWorld = SphereTransform.TransformPosition(Sphere.GetCenter());
			FVec3 SpherePosPlane = PlaneTransform.InverseTransformPosition(SpherePosWorld);

			FVec3 NormalPlane;
			FReal Phi = Plane.PhiWithNormal(SpherePosPlane, NormalPlane) - SphereRadius - 0.5f * ShapePadding;	// Adding plane's share of padding
			FVec3 NormalWorld = PlaneTransform.TransformVector(NormalPlane);
			FVec3 Location = SpherePosWorld - SphereRadius * NormalWorld;

			// @todo(chaos): margin
			Result.ShapeMargins[0] = 0.0f;
			Result.ShapeMargins[1] = 0.0f;
			Result.ShapeContactPoints[0] = SphereTransform.InverseTransformPosition(Location);
			Result.ShapeContactPoints[1] = PlaneTransform.InverseTransformPosition(Location - Phi * NormalWorld);
			Result.ShapeContactNormal = PlaneTransform.InverseTransformVector(NormalWorld);
			Result.Phi = Phi;
			Result.Normal = NormalWorld;
			Result.Location = Location;

			return Result;
		}

		void UpdateSpherePlaneConstraint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const TPlane<FReal, 3>& Plane, const FRigidTransform3& PlaneTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint, SpherePlaneContactPoint(Sphere, SphereTransform, Plane, PlaneTransform, Constraint.Manifold.RestitutionPadding), Dt);
		}

		template<typename T_TRAITS>
		void ConstructSpherePlaneConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const TPlane<FReal, 3>* Object1 = Implicit1->template GetObject<const TPlane<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SpherePlane, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSpherePlaneConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		//
		// Sphere - Box
		//


		FContactPoint SphereBoxContactPoint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FImplicitBox3& Box, const FRigidTransform3& BoxTransform, const FReal ShapePadding)
		{
			FContactPoint Result;

			const FVec3 SphereWorld = SphereTransform.TransformPosition(Sphere.GetCenter());	// World-space sphere pos
			const FVec3 SphereBox = BoxTransform.InverseTransformPosition(SphereWorld);			// Box-space sphere pos

			FVec3 NormalBox;																	// Box-space normal
			FReal PhiToSphereCenter = Box.PhiWithNormal(SphereBox, NormalBox);
			FReal Phi = PhiToSphereCenter - Sphere.GetRadius() - ShapePadding;
			
			FVec3 NormalWorld = BoxTransform.TransformVectorNoScale(NormalBox);
			FVec3 LocationWorld = SphereWorld - (Sphere.GetRadius() + 0.5f * ShapePadding) * NormalWorld;

			// @todo(chaos): margin
			Result.ShapeMargins[0] = 0.0f;
			Result.ShapeMargins[1] = 0.0f;
			Result.ShapeContactPoints[0] = SphereTransform.InverseTransformPosition(LocationWorld);
			Result.ShapeContactPoints[1] = BoxTransform.InverseTransformPosition(LocationWorld - Phi * NormalWorld);
			Result.ShapeContactNormal = NormalBox;
			Result.Phi = Phi;
			Result.Normal = NormalWorld;
			Result.Location = LocationWorld;
			return Result;
		}


		void UpdateSphereBoxConstraint(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FImplicitBox3& Box, const FRigidTransform3& BoxTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint, SphereBoxContactPoint(Sphere, SphereTransform, Box, BoxTransform, Constraint.Manifold.RestitutionPadding), Dt);
		}

		template<typename T_TRAITS>
		void ConstructSphereBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{

			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const TBox<FReal, 3>* Object1 = Implicit1->template GetObject<const TBox<FReal, 3> >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereBox, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereBoxConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}


		//
		// Sphere - Capsule
		//

		FContactPoint SphereCapsuleContactPoint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const FCapsule& B, const FRigidTransform3& BTransform, const FReal ShapePadding)
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
				FReal NewPhi = DeltaLen - (A.GetRadius() + B.GetRadius()) - ShapePadding;
				FVec3 Dir = Delta / DeltaLen;
				FVec3 LocationA = A1 + Dir * A.GetRadius();
				FVec3 LocationB = P2 - Dir * B.GetRadius();
				FVec3 Normal = -Dir;
				// @todo(chaos): margin
				Result.ShapeMargins[0] = 0.0f;
				Result.ShapeMargins[1] = 0.0f;
				Result.ShapeContactPoints[0] = ATransform.InverseTransformPosition(LocationA);
				Result.ShapeContactPoints[1] = BTransform.InverseTransformPosition(LocationB);
				Result.ShapeContactNormal = BTransform.InverseTransformVector(Normal);
				Result.Phi = NewPhi;
				Result.Normal = Normal;
				Result.Location = 0.5f * (LocationA + LocationB);
			}

			return Result;
		}


		void UpdateSphereCapsuleConstraint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const FCapsule& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint, SphereCapsuleContactPoint(A, ATransform, B, BTransform, Constraint.Manifold.RestitutionPadding), Dt);
		}

		template<typename T_TRAITS>
		void ConstructSphereCapsuleConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const FCapsule* Object1 = Implicit1->template GetObject<const FCapsule >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereCapsule, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereCapsuleConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		//
		// Sphere-Convex
		//

		void UpdateSphereConvexConstraint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const FImplicitObject3& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint, SphereConvexContactPoint(A, ATransform, B, BTransform), Dt);
		}

		template<typename T_TRAITS>
		void ConstructSphereConvexConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			const FImplicitObject* Object1 = Implicit1;
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereConvex, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereConvexConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		//
		// Sphere-TriangleMesh
		//

		template <typename TriMeshType>
		FContactPoint SphereTriangleMeshContactPoint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			return GJKImplicitContactPoint<TSphere<FReal, 3>>(TSphere<FReal, 3>(A), ATransform, B, BTransform, CullDistance, ShapePadding);
		}

		template<typename TriMeshType>
		FContactPoint SphereTriangleMeshSweptContactPoint(const TSphere<FReal, 3>& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BStartTransform, const FVec3& Dir, const FReal Length, FReal& TOI)
		{
			if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = B.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
			{
				return GJKImplicitScaledTriMeshSweptContactPoint<TSphere<FReal, 3>>(A, ATransform, *ScaledTriangleMesh, BStartTransform, Dir, Length, TOI);
			}
			else if (const FTriangleMeshImplicitObject* TriangleMesh = B.template GetObject<const FTriangleMeshImplicitObject>())
			{
				return GJKImplicitSweptContactPoint<TSphere<FReal, 3>>(A, ATransform, *TriangleMesh, BStartTransform, Dir, Length, TOI);
			}

			ensure(false);
			return FContactPoint();
		}

		template <typename TriMeshType>
		void UpdateSphereTriangleMeshConstraint(const TSphere<FReal, 3>& Sphere0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(chaos): restitutionpadding
			UpdateContactPoint(Constraint, SphereTriangleMeshContactPoint(Sphere0, WorldTransform0, TriangleMesh1, WorldTransform1, Constraint.GetCullDistance(), 0.0f), Dt);
		}

		template<typename TriMeshType>
		void UpdateSphereTriangleMeshConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const TSphere<FReal, 3>& Sphere0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FVec3& Dir, const FReal Length, const FReal Dt, FRigidBodySweptPointContactConstraint& Constraint)
		{
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint, SphereTriangleMeshSweptContactPoint(Sphere0, WorldTransform0, TriangleMesh1, WorldTransform1, Dir, Length, TOI), Dt);
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template<typename T_TRAITS>
		void ConstructSphereTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3> >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereTriMesh, Context.bAllowManifolds);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateSphereTriangleMeshConstraint(*Object0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, Dt, Constraint);
					}
					NewConstraints.Add(Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereTriMesh, Context.bAllowManifolds);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateSphereTriangleMeshConstraint(*Object0, WorldTransform0, *TriangleMesh, WorldTransform1, Dt, Constraint);
					}
					NewConstraints.Add(Constraint);
				}
				else
				{
					ensure(false);
				}
			}
		}


		void ConstructSphereTriangleMeshConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			const TSphere<FReal, 3>* Object0 = Implicit0->template GetObject<const TSphere<FReal, 3>>();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransformX0, *ScaledTriangleMesh, WorldTransform1, Dir, Length, Dt, Constraint);
					NewConstraints.Add(Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::SphereTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateSphereTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransformX0, *TriangleMesh, WorldTransform1, Dir, Length, Dt, Constraint);
					NewConstraints.Add(Constraint);
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


		FContactPoint CapsuleCapsuleContactPoint(const FCapsule& A, const FRigidTransform3& ATransform, const FCapsule& B, const FRigidTransform3& BTransform, const FReal ShapePadding)
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
				FReal NewPhi = DeltaLen - (A.GetRadius() + B.GetRadius()) - ShapePadding;
				FVec3 Dir = Delta / DeltaLen;
				FVec3 Normal = -Dir;
				FVec3 LocationA = P1 + Dir * A.GetRadius();
				FVec3 LocationB = P2 - Dir * B.GetRadius();
				// @todo(chaos): margin
				Result.ShapeMargins[0] = 0.0f;
				Result.ShapeMargins[1] = 0.0f;
				Result.ShapeContactPoints[0] = ATransform.InverseTransformPosition(LocationA);
				Result.ShapeContactPoints[1] = BTransform.InverseTransformPosition(LocationB);
				Result.ShapeContactNormal = BTransform.InverseTransformVector(Normal);
				Result.Phi = NewPhi;
				Result.Normal = Normal;
				Result.Location = 0.5f * (LocationA + LocationB);
			}

			return Result;
		}


		void UpdateCapsuleCapsuleConstraint(const FCapsule& A, const FRigidTransform3& ATransform, const FCapsule& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint, CapsuleCapsuleContactPoint(A, ATransform, B, BTransform, Constraint.Manifold.RestitutionPadding), Dt);
		}

		template<typename T_TRAITS>
		void ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const FCapsule* Object0 = Implicit0->template GetObject<const FCapsule >();
			const FCapsule* Object1 = Implicit1->template GetObject<const FCapsule >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleCapsule, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleCapsuleConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		//
		// Capsule - Box
		//


		FContactPoint CapsuleBoxContactPoint(const FCapsule& A, const FRigidTransform3& ATransform, const FImplicitBox3& B, const FRigidTransform3& BTransform, const FVec3& InitialDir, const FReal ShapePadding)
		{
			return GJKContactPoint(A, ATransform, B, BTransform, InitialDir, ShapePadding);
		}


		void UpdateCapsuleBoxConstraint(const FCapsule& A, const FRigidTransform3& ATransform, const FImplicitBox3& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			const FVec3 InitialDir = ATransform.GetRotation().Inverse() * -Constraint.GetNormal();
			UpdateContactPoint(Constraint, CapsuleBoxContactPoint(A, ATransform, B, BTransform, InitialDir, Constraint.Manifold.RestitutionPadding), Dt);
		}

		// @todo(chaos): make this work with the new one-shot manifold
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
		//void UpdateCapsuleBoxManifold(const FCapsule& Capsule, const FRigidTransform3& CapsuleTM, const FImplicitBox3& Box, const FRigidTransform3& BoxTM, const FReal CullDistance, FRigidBodyPointContactConstraint& Constraint)
		//{
		//	// Initialize with an empty manifold. If we early-out this leaves the manifold as created but not usable to
		//	// support the fallback to geometry-based collision detection
		//	Constraint.InitManifold();

		//	// Capture the state required to invalidate the manifold if things move too much
		//	const FReal ManifoldPositionTolerance = Chaos_Collision_ManifoldPositionTolerance;
		//	const FReal ManifoldRotationTolerance = Chaos_Collision_ManifoldRotationTolerance;
		//	Constraint.InitManifoldTolerance(CapsuleTM, BoxTM, ManifoldPositionTolerance, ManifoldRotationTolerance);

		//	// Find the nearest points on the capsule and box
		//	// Note: We flip the order for GJK so we get the normal in box space. This makes it easier to build the face-capsule manifold.
		//	const FRigidTransform3 CapsuleToBoxTM = CapsuleTM.GetRelativeTransform(BoxTM);

		//	// NOTE: All GJK results in box-space
		//	// @todo(ccaulfield): use center-to-center direction for InitialDir
		//	FVec3 InitialDir = FVec3(1, 0, 0);
		//	FReal Penetration;
		//	FVec3 CapsuleClosestBoxSpace, BoxClosestBoxSpace, NormalBoxSpace;
		//	int32 ClosestVertexIndexBox, ClosestVertexIndexCapsule;
		//	{
		//		SCOPE_CYCLE_COUNTER_GJK();
		//		if (!ensure(GJKPenetration<true>(Box, Capsule, CapsuleToBoxTM, Penetration, BoxClosestBoxSpace, CapsuleClosestBoxSpace, NormalBoxSpace, ClosestVertexIndexBox, ClosestVertexIndexCapsule, (FReal)0, (FReal)0, InitialDir)))
		//		{
		//			return;
		//		}
		//	}

		//	// Cache the closest point so we don't need to re-iterate over the manifold on the first iteration.
		//	Constraint.Manifold.Location = BoxTM.TransformPosition(BoxClosestBoxSpace);
		//	Constraint.Manifold.Normal = BoxTM.TransformVectorNoScale(NormalBoxSpace);
		//	Constraint.Manifold.Phi = -Penetration;

		//	// Find the box feature that the near point is on
		//	// Face, Edge, or Vertex can be determined from number of non-zero elements in the box-space normal.
		//	const FReal ComponentEpsilon = Chaos_Collision_ManifoldFaceEpsilon;
		//	int32 NumNonZeroNormalComponents = 0;
		//	int32 MaxComponentIndex = INDEX_NONE;
		//	FReal MaxComponentValue = 0;
		//	for (int32 ComponentIndex = 0; ComponentIndex < 3; ++ComponentIndex)
		//	{
		//		FReal AbsComponentValue = FMath::Abs(NormalBoxSpace[ComponentIndex]);
		//		if (AbsComponentValue > ComponentEpsilon)
		//		{
		//			++NumNonZeroNormalComponents;
		//		}
		//		if (AbsComponentValue > MaxComponentValue)
		//		{
		//			MaxComponentValue = AbsComponentValue;
		//			MaxComponentIndex = ComponentIndex;
		//		}
		//	}

		//	// Make sure we actually have a feature to use
		//	if (!ensure(MaxComponentIndex != INDEX_NONE))
		//	{
		//		return;
		//	}

		//	FVec3 CapsuleAxis = CapsuleToBoxTM.TransformVectorNoScale(Capsule.GetAxis()); // Box space capsule axis
		//	FReal CapsuleAxisNormal = FVec3::DotProduct(CapsuleAxis, NormalBoxSpace);
		//	const bool bIsBoxFaceContact = (NumNonZeroNormalComponents == 1);
		//	const bool bIsBoxEdgeContact = (NumNonZeroNormalComponents == 2);
		//	const bool bIsBoxVertexContact = (NumNonZeroNormalComponents == 3);
		//	const bool bIsCapsuleEdgeContact = (CapsuleAxisNormal < ComponentEpsilon);
		//	const bool bIsCapsuleVertexContact = !bIsCapsuleEdgeContact;

		//	// We just use the nearest point for these combinations, with the box as the plane owner
		//	if (bIsBoxVertexContact || (bIsBoxEdgeContact && bIsCapsuleVertexContact))
		//	{
		//		Constraint.SetManifoldPlane(1, INDEX_NONE, NormalBoxSpace, BoxClosestBoxSpace);
		//		Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleClosestBoxSpace));
		//		return;
		//	}

		//	// Get the radial direction from the capsule axis to the box face
		//	FVec3 CapsuleRadiusDir = FVec3::CrossProduct(FVec3::CrossProduct(NormalBoxSpace, CapsuleAxis), CapsuleAxis);
		//	if (!Utilities::NormalizeSafe(CapsuleRadiusDir))
		//	{
		//		// Capsule is perpendicular to the face, just leave the one-point manifold
		//		Constraint.SetManifoldPlane(1, INDEX_NONE, NormalBoxSpace, BoxClosestBoxSpace);
		//		Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleClosestBoxSpace));
		//		return;
		//	}

		//	// This is the axis of the box face we will be using below
		//	int32 FaceAxisIndex = MaxComponentIndex;

		//	// If this is a box face contact, the box is the manifold plane owner, regardless of the capsule feature involved. 
		//	// We take the edge of the capsule nearest the face, clip it to the face, and use the clipped points as the manifold points.
		//	// NOTE: The plane normal is not exactly the face normal, so this is not strictly the correct manifold. In particular
		//	// it will over-limit the rotation for this frame. We could fix this by reverse-projecting the edge points based on the angle...
		//	if (bIsBoxFaceContact)
		//	{
		//		// Initialize the manifold with the nearest plane and nearest point
		//		Constraint.SetManifoldPlane(1, INDEX_NONE, NormalBoxSpace, BoxClosestBoxSpace);
		//		Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleClosestBoxSpace));

		//		// Calculate the capsule edge nearest the face
		//		FVec3 CapsuleVert0 = CapsuleToBoxTM.TransformPosition(Capsule.GetX1()) + Capsule.GetMargin() * CapsuleRadiusDir;
		//		FVec3 CapsuleVert1 = CapsuleToBoxTM.TransformPosition(Capsule.GetX2()) + Capsule.GetMargin() * CapsuleRadiusDir;

		//		// Clip the capsule edge to the axis-aligned box face
		//		for (int32 ClipAxisIndex = 0; ClipAxisIndex < 3; ++ClipAxisIndex)
		//		{
		//			if (ClipAxisIndex != FaceAxisIndex)
		//			{
		//				bool bAcceptedPos = Utilities::ClipLineSegmentToAxisAlignedPlane(CapsuleVert0, CapsuleVert1, ClipAxisIndex, 1.0f, Box.Max()[ClipAxisIndex]);
		//				bool bAcceptedNeg = Utilities::ClipLineSegmentToAxisAlignedPlane(CapsuleVert0, CapsuleVert1, ClipAxisIndex, -1.0f, Box.Min()[ClipAxisIndex]);
		//				if (!bAcceptedPos || !bAcceptedNeg)
		//				{
		//					// Capsule edge is outside a face - stick with the single point we have
		//					return;
		//				}
		//			}
		//		}

		//		// Add the clipped points to the manifold if they are not too close to each other, or the near point calculated above.
		//		// Note: verts are in box space - need to be in capsule space
		//		// @todo(chaos): manifold point distance tolerance should be a per-solver or per object setting
		//		const FReal DistanceToleranceSq = (0.1f * Capsule.GetHeight()) * (0.1f * Capsule.GetHeight());
		//		bool bUseVert0 = ((CapsuleVert1 - CapsuleVert0).SizeSquared() > DistanceToleranceSq) && ((CapsuleVert0 - CapsuleClosestBoxSpace).SizeSquared() > DistanceToleranceSq);
		//		bool bUseVert1 = ((CapsuleVert1 - CapsuleClosestBoxSpace).SizeSquared() > DistanceToleranceSq);
		//		if (bUseVert0)
		//		{
		//			Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleVert0));
		//		}
		//		if (bUseVert1)
		//		{
		//			Constraint.AddManifoldPoint(CapsuleToBoxTM.InverseTransformPosition(CapsuleVert1));
		//		}

		//		return;
		//	}

		//	// If the box edge and the capsule edge are the closest features, we treat the capsule as the
		//	// plane owner. Then select the most-parallel box face as the source of manifold points. The
		//	// manifold points are found by projecting the capsule verts onto the face and clipping to it.
		//	// We have a capsule edge if the normal is perpendicular to the capsule axis
		//	if (bIsBoxEdgeContact && bIsCapsuleEdgeContact)
		//	{
		//		// Initialize the manifold with the plane and point on the capsule (move to capsule space)
		//		FVec3 NormalCapsuleSpace = CapsuleToBoxTM.InverseTransformVectorNoScale(NormalBoxSpace);
		//		FVec3 CapsuleClosestCapsuleSpace = CapsuleToBoxTM.InverseTransformPosition(CapsuleClosestBoxSpace);
		//		Constraint.SetManifoldPlane(0, INDEX_NONE, -NormalCapsuleSpace, CapsuleClosestCapsuleSpace);
		//		Constraint.AddManifoldPoint(BoxClosestBoxSpace);

		//		// Project the capsule edge onto the box face
		//		FReal FaceAxisSign = FMath::Sign(NormalBoxSpace[FaceAxisIndex]);
		//		FReal FacePos = (FaceAxisSign >= 0.0f) ? Box.Max()[FaceAxisIndex] : Box.Min()[FaceAxisIndex];
		//		FVec3 CapsuleVert0 = CapsuleToBoxTM.TransformPosition(Capsule.GetX1());
		//		FVec3 CapsuleVert1 = CapsuleToBoxTM.TransformPosition(Capsule.GetX2());
		//		Utilities::ProjectPointOntoAxisAlignedPlane(CapsuleVert0, CapsuleRadiusDir, FaceAxisIndex, FaceAxisSign, FacePos);
		//		Utilities::ProjectPointOntoAxisAlignedPlane(CapsuleVert1, CapsuleRadiusDir, FaceAxisIndex, FaceAxisSign, FacePos);

		//		// Clip the capsule edge to the axis-aligned box face
		//		for (int32 ClipAxisIndex = 0; ClipAxisIndex < 3; ++ClipAxisIndex)
		//		{
		//			if (ClipAxisIndex != FaceAxisIndex)
		//			{
		//				bool bAcceptedPos = Utilities::ClipLineSegmentToAxisAlignedPlane(CapsuleVert0, CapsuleVert1, ClipAxisIndex, 1.0f, Box.Max()[ClipAxisIndex]);
		//				bool bAcceptedNeg = Utilities::ClipLineSegmentToAxisAlignedPlane(CapsuleVert0, CapsuleVert1, ClipAxisIndex, -1.0f, Box.Min()[ClipAxisIndex]);
		//				if (!bAcceptedPos || !bAcceptedNeg)
		//				{
		//					// Capsule edge is outside a face - stick with the single point we have
		//					return;
		//				}
		//			}
		//		}

		//		// Add the clipped points to the manifold if they are not too close to each other, or the near point calculated above.
		//		// Note: verts are in box space - need to be in capsule space
		//		// @todo(chaos): manifold point distance tolerance should be a per-solver or per object setting
		//		const FReal DistanceToleranceSq = (0.1f * Capsule.GetHeight()) * (0.1f * Capsule.GetHeight());
		//		bool bUseVert0 = ((CapsuleVert1 - CapsuleVert0).SizeSquared() > DistanceToleranceSq) && ((CapsuleVert0 - BoxClosestBoxSpace).SizeSquared() > DistanceToleranceSq);
		//		bool bUseVert1 = ((CapsuleVert1 - BoxClosestBoxSpace).SizeSquared() > DistanceToleranceSq);
		//		if (bUseVert0)
		//		{
		//			Constraint.AddManifoldPoint(CapsuleVert0);
		//		}
		//		if (bUseVert1)
		//		{
		//			Constraint.AddManifoldPoint(CapsuleVert1);
		//		}

		//		return;
		//	}

		//	// All feature combinations should be covered above
		//	ensure(false);
		//}

		template<typename T_TRAITS>
		void ConstructCapsuleBoxConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const FCapsule* Object0 = Implicit0->template GetObject<const FCapsule>();
			const TBox<FReal, 3>* Object1 = Implicit1->template GetObject<const TBox<FReal, 3>>();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleBox, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleBoxConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}


		//
		// Capsule-Convex
		//

		void UpdateCapsuleConvexConstraint(const FCapsule& A, const FRigidTransform3& ATransform, const FImplicitObject& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			UpdateContactPoint(Constraint, CapsuleConvexContactPoint(A, ATransform, B, BTransform), Dt);
		}

		template<typename T_TRAITS>
		void ConstructCapsuleConvexConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			const FCapsule* Object0 = Implicit0->template GetObject<const FCapsule>();
			const FImplicitObject* Object1 = Implicit1;
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleConvex, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleConvexConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}


		//
		// Capsule-HeightField
		//


		FContactPoint CapsuleHeightFieldContactPoint(const FCapsule& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_CapsuleHeightFieldContactPoint, ConstraintsDetailedStats);
			return GJKImplicitContactPoint<FCapsule>(A, ATransform, B, BTransform, CullDistance, ShapePadding);
		}


		void UpdateCapsuleHeightFieldConstraint(const FCapsule& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(chaos): restitutionpadding
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateCapsuleHeightFieldConstraint, ConstraintsDetailedStats);
			UpdateContactPoint(Constraint, CapsuleHeightFieldContactPoint(A, ATransform, B, BTransform, Constraint.GetCullDistance(), 0.0f), Dt);
		}


		void UpdateCapsuleHeightFieldConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const FCapsule& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, const FReal Dt, FRigidBodySweptPointContactConstraint& Constraint)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateCapsuleHeightFieldConstraintSwept, ConstraintsDetailedStats);
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint, GJKImplicitSweptContactPoint<FCapsule>(A, ATransform, B, BTransform, Dir, Length, TOI), Dt);
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template<typename T_TRAITS>
		void ConstructCapsuleHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructCapsuleHeightFieldConstraints, ConstraintsDetailedStats);

			const FCapsule* Object0 = Implicit0->template GetObject<const FCapsule >();
			const FHeightField* Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleHeightField, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleHeightFieldConstraint(*Object0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		void ConstructCapsuleHeightFieldConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructCapsuleHeightFieldConstraintsSwept, ConstraintsDetailedStats);
			const FCapsule* Object0 = Implicit0->template GetObject<const FCapsule >();
			const FHeightField* Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Object0 && Object1))
			{
				FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleHeightField);
				FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
				FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
				UpdateCapsuleHeightFieldConstraintSwept(Particle0, *Object0, WorldTransformX0, *Object1, WorldTransform1, Dir, Length, Dt, Constraint);
				NewConstraints.Add(Constraint);
			}
		}


		//
		// Capsule-TriangleMesh
		//

		template <typename TriMeshType>
		FContactPoint CapsuleTriangleMeshContactPoint(const FCapsule& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_CapsuleTriangleMeshContactPoint, ConstraintsDetailedStats);
			return GJKImplicitContactPoint<FCapsule>(A, ATransform, B, BTransform, CullDistance, ShapePadding);
		}

		template <typename TriMeshType>
		FContactPoint CapsuleTriangleMeshSweptContactPoint(const FCapsule& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BStartTransform, const FVec3& Dir, const FReal Length, FReal& TOI)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_CapsuleTriangleMeshSweptContactPoint, ConstraintsDetailedStats);
			if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = B.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
			{
				return GJKImplicitScaledTriMeshSweptContactPoint<FCapsule>(A, ATransform, *ScaledTriangleMesh, BStartTransform, Dir, Length, TOI);
			}
			else if (const FTriangleMeshImplicitObject* TriangleMesh = B.template GetObject<const FTriangleMeshImplicitObject>())
			{
				return GJKImplicitSweptContactPoint<FCapsule>(A, ATransform, *TriangleMesh, BStartTransform, Dir, Length, TOI);
			}

			ensure(false);
			return FContactPoint();
		}


		template <typename TriMeshType>
		void UpdateCapsuleTriangleMeshConstraint(const FCapsule& Capsule0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(chaos): restitutionpadding
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateCapsuleTriangleMeshConstraint, ConstraintsDetailedStats);
			UpdateContactPoint(Constraint, CapsuleTriangleMeshContactPoint(Capsule0, WorldTransform0, TriangleMesh1, WorldTransform1, Constraint.GetCullDistance(), 0.0f), Dt);
		}

		template <typename TriMeshType>
		void UpdateCapsuleTriangleMeshConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const FCapsule& Capsule0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FVec3& Dir, const FReal Length, const FReal Dt, FRigidBodySweptPointContactConstraint& Constraint)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateCapsuleTriangleMeshConstraint, ConstraintsDetailedStats);
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint, CapsuleTriangleMeshSweptContactPoint(Capsule0, WorldTransform0, TriangleMesh1, WorldTransform1, Dir, Length, TOI), Dt);
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template<typename T_TRAITS>
		void ConstructCapsuleTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructCapsuleTriangleMeshConstraints, ConstraintsDetailedStats);

			const FCapsule* Object0 = Implicit0->template GetObject<const FCapsule >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleTriMesh, Context.bAllowManifolds);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateCapsuleTriangleMeshConstraint(*Object0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, Dt, Constraint);
					}
					NewConstraints.Add(Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleTriMesh, Context.bAllowManifolds);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateCapsuleTriangleMeshConstraint(*Object0, WorldTransform0, *TriangleMesh, WorldTransform1, Dt, Constraint);
					}
					NewConstraints.Add(Constraint);
				}
				else
				{
					ensure(false);
				}
			}
		}

		void ConstructCapsuleTriangleMeshConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructCapsuleTriangleMeshConstraintsSwept, ConstraintsDetailedStats);

			const FCapsule* Object0 = Implicit0->template GetObject<const FCapsule >();
			if (ensure(Object0))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransformX0, *ScaledTriangleMesh, WorldTransform1, Dir, Length, Dt, Constraint);
					NewConstraints.Add(Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::CapsuleTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateCapsuleTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransformX0, *TriangleMesh, WorldTransform1, Dir, Length, Dt, Constraint);
					NewConstraints.Add(Constraint);
				}
				else
				{
					ensure(false);
				}
			}
		}

		//
		// Generic Convex - Convex (actualy concrete type could be anything)
		//

		void UpdateGenericConvexConvexConstraint(const FImplicitObject& Implicit0, const FRigidTransform3& WorldTransform0, const FImplicitObject& Implicit1, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateGenericConvexConvexConstraint, ConstraintsDetailedStats);

			UpdateGenericConvexConvexConstraintHelper(Implicit0, WorldTransform0, Implicit1, WorldTransform1, Dt, Constraint.Manifold.RestitutionPadding, Constraint);
		}

		void UpdateGenericConvexConvexConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const FImplicitObject& Implicit0, const FRigidTransform3& WorldTransform0, const FImplicitObject& Implicit1, const FRigidTransform3& WorldTransform1, const FVec3& Dir, const FReal Length, const FReal Dt, FRigidBodySweptPointContactConstraint& Constraint)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateGenericConvexConvexConstraintSwept, ConstraintsDetailedStats);
			FReal TOI = 1.0f;
			UpdateContactPointNoCull(Constraint, GenericConvexConvexContactPointSwept(Implicit0, WorldTransform0, Implicit1, WorldTransform1, Dir, Length, TOI), Dt);
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}


		template<typename T_TRAITS>
		void ConstructGenericConvexConvexConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructGenericConvexConvexConstraints, ConstraintsDetailedStats);
			EImplicitObjectType Implicit0Type = Particle0->Geometry()->GetType();
			EImplicitObjectType Implicit1Type = Particle1->Geometry()->GetType();

			FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::GenericConvexConvex, Context.bAllowManifolds);
			if (T_TRAITS::bImmediateUpdate)
			{
				FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
				FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
				UpdateGenericConvexConvexConstraint(*Implicit0, WorldTransform0, *Implicit1, WorldTransform1, Dt, Constraint);
			}
			NewConstraints.Add(Constraint);
		}

		void ConstructGenericConvexConvexConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructGenericConvexConvexConstraintsSwept, ConstraintsDetailedStats);
			FGenericParticleHandle RigidParticle = FGenericParticleHandle(Particle0);
			FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::GenericConvexConvex);
			// Note: This is unusual but we are using a mix of the previous and current transform
			// This is due to how CCD only rewinds the position
			FRigidTransform3 WorldTransformXQ0 = LocalTransform0 * FRigidTransform3(RigidParticle->X(), RigidParticle->Q());
			FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
			UpdateGenericConvexConvexConstraintSwept(Particle0, *Implicit0, WorldTransformXQ0, *Implicit1, WorldTransform1, Dir, Length, Dt, Constraint);
			NewConstraints.Add(Constraint);
		}

		//
		// Convex - HeightField
		//


		FContactPoint ConvexHeightFieldContactPoint(const FImplicitObject& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConvexHeightFieldContactPoint, ConstraintsDetailedStats);
			return GJKImplicitContactPoint<FConvex>(A, ATransform, B, BTransform, CullDistance, ShapePadding);
		}


		void UpdateConvexHeightFieldConstraint(const FImplicitObject& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(chaos): restitutionpadding
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateConvexHeightFieldConstraint, ConstraintsDetailedStats);
			UpdateContactPoint(Constraint, ConvexHeightFieldContactPoint(A, ATransform, B, BTransform, Constraint.GetCullDistance(), 0.0f), Dt);
		}


		void UpdateConvexHeightFieldConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const FImplicitObject& A, const FRigidTransform3& ATransform, const FHeightField& B, const FRigidTransform3& BTransform, const FVec3& Dir, const FReal Length, const FReal Dt, FRigidBodySweptPointContactConstraint& Constraint)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateConvexHeightFieldConstraintSwept, ConstraintsDetailedStats);
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint, GJKImplicitSweptContactPoint< FConvex >(A, ATransform, B, BTransform, Dir, Length, TOI), Dt);
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template<typename T_TRAITS>
		void ConstructConvexHeightFieldConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructConvexHeightFieldConstraints, ConstraintsDetailedStats);
			const FHeightField* Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Implicit0->IsConvex() && Object1))
			{
				FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::ConvexHeightField, Context.bAllowManifolds);
				if (T_TRAITS::bImmediateUpdate)
				{
					FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateConvexHeightFieldConstraint(*Implicit0, WorldTransform0, *Object1, WorldTransform1, Dt, Constraint);
				}
				NewConstraints.Add(Constraint);
			}
		}

		void ConstructConvexHeightFieldConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructConvexHeightFieldConstraintsSwept, ConstraintsDetailedStats);
			const FHeightField* Object1 = Implicit1->template GetObject<const FHeightField >();
			if (ensure(Implicit0->IsConvex() && Object1))
			{
				FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::ConvexHeightField);
				FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
				FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
				UpdateConvexHeightFieldConstraintSwept(Particle0, *Implicit0, WorldTransformX0, *Object1, WorldTransform1, Dir, Length, Dt, Constraint);
				NewConstraints.Add(Constraint);
			}
		}

		//
		// Convex-TriangleMesh
		//

		template <typename TriMeshType>
		FContactPoint ConvexTriangleMeshContactPoint(const FImplicitObject& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BTransform, const FReal CullDistance, const FReal ShapePadding)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConvexTriangleMeshContactPoint, ConstraintsDetailedStats);
			return GJKImplicitContactPoint<FConvex>(A, ATransform, B, BTransform, CullDistance, ShapePadding);
		}

		template <typename TriMeshType>
		FContactPoint ConvexTriangleMeshSweptContactPoint(const FImplicitObject& A, const FRigidTransform3& ATransform, const TriMeshType& B, const FRigidTransform3& BStartTransform, const FVec3& Dir, const FReal Length, FReal& TOI)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConvexTriangleMeshSweptContactPoint, ConstraintsDetailedStats);
			if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = B.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
			{
				return GJKImplicitScaledTriMeshSweptContactPoint<FConvex>(A, ATransform, *ScaledTriangleMesh, BStartTransform, Dir, Length, TOI);
			}
			else if (const FTriangleMeshImplicitObject* TriangleMesh = B.template GetObject<const FTriangleMeshImplicitObject>())
			{
				return GJKImplicitSweptContactPoint<FConvex>(A, ATransform, *TriangleMesh, BStartTransform, Dir, Length, TOI);
			}

			ensure(false);
			return FContactPoint();
		}

		template <typename TriMeshType>
		void UpdateConvexTriangleMeshConstraint(const FImplicitObject& Convex0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			// @todo(chaos): restitutionpadding
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateConvexTriangleMeshConstraint, ConstraintsDetailedStats);
			UpdateContactPoint(Constraint, ConvexTriangleMeshContactPoint(Convex0, WorldTransform0, TriangleMesh1, WorldTransform1, Constraint.GetCullDistance(), 0.0f), Dt);
		}

		// Sweeps convex against trimesh
		template <typename TriMeshType>
		void UpdateConvexTriangleMeshConstraintSwept(TGeometryParticleHandle<FReal, 3>* Particle0, const FImplicitObject& Convex0, const FRigidTransform3& WorldTransform0, const TriMeshType& TriangleMesh1, const FRigidTransform3& WorldTransform1, const FVec3& Dir, const FReal Length, const FReal Dt, FRigidBodySweptPointContactConstraint& Constraint)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateConvexTriangleMeshConstraintSwept, ConstraintsDetailedStats);
			FReal TOI = 1.0f;
			UpdateContactPoint(Constraint, ConvexTriangleMeshSweptContactPoint(Convex0, WorldTransform0, TriangleMesh1, WorldTransform1, Dir, Length, TOI), Dt);
			SetSweptConstraintTOI(Particle0, TOI, Length, Dir, Constraint);
		}

		template<typename T_TRAITS>
		void ConstructConvexTriangleMeshConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructConvexTriangleMeshConstraints, ConstraintsDetailedStats);
			if (ensure(Implicit0->IsConvex()))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform0, CullDistance, EContactShapesType::ConvexTriMesh, Context.bAllowManifolds);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateConvexTriangleMeshConstraint(*Implicit0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, Dt, Constraint);
					}
					NewConstraints.Add(Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::ConvexTriMesh, Context.bAllowManifolds);
					if (T_TRAITS::bImmediateUpdate)
					{
						FRigidTransform3 WorldTransform0 = LocalTransform0 * Collisions::GetTransform(Particle0);
						FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
						UpdateConvexTriangleMeshConstraint(*Implicit0, WorldTransform0, *TriangleMesh, WorldTransform1, Dt, Constraint);
					}
					NewConstraints.Add(Constraint);
				}
				else
				{
					ensure(false);
				}
			}
		}

		void ConstructConvexTriangleMeshConstraintsSwept(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FVec3& Dir, FReal Length, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructConvexTriangleMeshConstraintsSwept, ConstraintsDetailedStats);
			if (ensure(Implicit0->IsConvex()))
			{
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1->template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::ConvexTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateConvexTriangleMeshConstraintSwept(Particle0, *Implicit0, WorldTransformX0, *ScaledTriangleMesh, WorldTransform1, Dir, Length, Dt, Constraint);
					NewConstraints.Add(Constraint);
				}
				else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1->template GetObject<const FTriangleMeshImplicitObject>())
				{
					FRigidBodySweptPointContactConstraint Constraint = FRigidBodySweptPointContactConstraint(Particle0, Implicit0, nullptr, LocalTransform0, Particle1, Implicit1, nullptr, LocalTransform1, CullDistance, EContactShapesType::ConvexTriMesh);
					FRigidTransform3 WorldTransformX0 = LocalTransform0 * FRigidTransform3(Particle0->X(), Particle0->R());
					FRigidTransform3 WorldTransform1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					UpdateConvexTriangleMeshConstraintSwept(Particle0, *Implicit0, WorldTransformX0, *TriangleMesh, WorldTransform1, Dir, Length, Dt, Constraint);
					NewConstraints.Add(Constraint);
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

		template<ECollisionUpdateType UpdateType>
		void UpdateLevelsetLevelsetConstraint(const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetLevelsetConstraint);

			// @todo(chaos): get rid of this
			FRigidTransform3 ParticlesTM = WorldTransform0;
			if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
			{
				return;
			}
			FRigidTransform3 LevelsetTM = WorldTransform1;
			if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
			{
				return;
			}

			FGenericParticleHandle Particle0 = Constraint.Particle[0];
			const FBVHParticles* SampleParticles = Constraint.Manifold.Simplicial[0];
			if (!SampleParticles)
			{
				ParticlesTM = FRigidTransform3(Particle0->P(), Particle0->Q());
				SampleParticles = Particle0->CollisionParticles().Get();
			}

			if (SampleParticles)
			{
				const FImplicitObject* Obj1 = Constraint.Manifold.Implicit[1];
				FContactPoint ContactPoint = SampleObject<UpdateType>(*Obj1, LevelsetTM, *SampleParticles, ParticlesTM, Constraint.GetCullDistance());

				if (ContactPoint.IsSet())
				{
					// @todo(chaos): margin
					ContactPoint.ShapeMargins[0] = 0.0f;
					ContactPoint.ShapeMargins[1] = 0.0f;
					ContactPoint.ShapeContactPoints[0] = WorldTransform0.InverseTransformPosition(ContactPoint.Location);
					ContactPoint.ShapeContactPoints[1] = WorldTransform1.InverseTransformPosition(ContactPoint.Location - ContactPoint.Phi * ContactPoint.Normal);
					ContactPoint.ShapeContactNormal = WorldTransform1.InverseTransformVector(ContactPoint.Normal);
				}

				UpdateContactPoint(Constraint, ContactPoint, Dt);
			}
		}

		void UpdateLevelsetLevelsetManifold(FCollisionConstraintBase& Constraint, const FRigidTransform3& ATM, const FRigidTransform3& BTM)
		{
			// @todo(chaos) : Stub Update Manifold
			//   Stub function for updating the manifold prior to the Apply and ApplyPushOut
		}


		template<typename T_TRAITS>
		void ConstructLevelsetLevelsetConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			FRigidBodyPointContactConstraint Constraint = FRigidBodyPointContactConstraint(Particle0, Implicit0, Simplicial0, LocalTransform0, Particle1, Implicit1, Simplicial1, LocalTransform1, CullDistance, EContactShapesType::LevelSetLevelSet, Context.bAllowManifolds);

			bool bIsParticleDynamic0 = Particle0->CastToRigidParticle() && Particle0->ObjectState() == EObjectStateType::Dynamic;
			int32 P0NumCollisionParticles = Simplicial0 ? Simplicial0->Size() : Particle0->CastToRigidParticle()->CollisionParticlesSize();
			if (!Particle1->Geometry() || (bIsParticleDynamic0 && !P0NumCollisionParticles && Particle0->Geometry() && !Particle0->Geometry()->IsUnderlyingUnion()))
			{
				Constraint.Particle[0] = Particle1;
				Constraint.Particle[1] = Particle0;
				Constraint.ImplicitTransform[0] = LocalTransform1;
				Constraint.ImplicitTransform[1] = LocalTransform0;
				Constraint.SetManifold(Implicit1, Simplicial1, Implicit0, Simplicial0);
			}
			else
			{
				Constraint.Particle[0] = Particle0;
				Constraint.Particle[1] = Particle1;
				Constraint.ImplicitTransform[0] = LocalTransform0;
				Constraint.ImplicitTransform[1] = LocalTransform1;
				Constraint.SetManifold(Implicit0, Simplicial0, Implicit1, Simplicial1);
			}

			if (T_TRAITS::bImmediateUpdate)
			{
				FRigidTransform3 WorldTransform0 = Constraint.ImplicitTransform[0] * Collisions::GetTransform(Constraint.Particle[0]);
				FRigidTransform3 WorldTransform1 = Constraint.ImplicitTransform[1] * Collisions::GetTransform(Constraint.Particle[1]);
				UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest>(WorldTransform0, WorldTransform1, Dt, Constraint);
			}
			NewConstraints.Add(Constraint);
		}


		//
		// Constraint API
		//


		// Run collision detection for the specified constraint to update the nearest contact point.
		// NOTE: Transforms are world space shape transforms
		// @todo(chaos): use a lookup table?
		// @todo(chaos): add the missing cases below
		// @todo(chaos): see use GetInnerObject below - we should try to use the leaf types for all (currently only TriMesh needs this)
		template<ECollisionUpdateType UpdateType>
		inline void UpdateConstraintFromGeometryImpl(FRigidBodyPointContactConstraint& Constraint, const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal Dt)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_UpdateConstraintFromGeometryInternal, ConstraintsDetailedStats);

			const FImplicitObject& Implicit0 = *Constraint.Manifold.Implicit[0];
			const FImplicitObject& Implicit1 = *Constraint.Manifold.Implicit[1];

			// @todo(chaos): remove
			//const FVec3 OriginalContactPositionLocal0 = WorldTransform0.InverseTransformPosition(Constraint.Manifold.Location);
			//const FVec3 OriginalContactPositionLocal1 = WorldTransform1.InverseTransformPosition(Constraint.Manifold.Location);

			if (Implicit0.HasBoundingBox() && Implicit1.HasBoundingBox())
			{
				const FReal CullDistance = Constraint.GetCullDistance();
				if (Chaos_Collision_NarrowPhase_SphereBoundsCheck)
				{
					const FReal R1 = Implicit0.BoundingBox().OriginRadius();
					const FReal R2 = Implicit1.BoundingBox().OriginRadius();
					const FReal SeparationSq = (WorldTransform1.GetTranslation() - WorldTransform0.GetTranslation()).SizeSquared();
					if (SeparationSq > FMath::Square(R1 + R2 + CullDistance))
					{
						return;
					}
				}

				if (Chaos_Collision_NarrowPhase_AABBBoundsCheck)
				{
					const FRigidTransform3 Box2ToBox1TM = WorldTransform1.GetRelativeTransform(WorldTransform0);
					const FAABB3 Box1 = Implicit0.BoundingBox();
					const FAABB3 Box2In1 = Implicit1.BoundingBox().TransformedAABB(Box2ToBox1TM).Thicken(CullDistance);
					if (!Box1.Intersects(Box2In1))
					{
						return;
					}
				}
			}


			switch (Constraint.Manifold.ShapesType)
			{
			case EContactShapesType::SphereSphere:
				UpdateSphereSphereConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<TSphere<FReal, 3>>(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::SphereCapsule:
				UpdateSphereCapsuleConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<FCapsule>(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::SphereBox:
				UpdateSphereBoxConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<TBox<FReal, 3>>(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::SphereConvex:
				UpdateSphereConvexConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, Implicit1, WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::SphereTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateSphereTriangleMeshConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *ScaledTriMesh, WorldTransform1, Dt, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateSphereTriangleMeshConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *TriangleMeshImplicit, WorldTransform1, Dt, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::SphereHeightField:
				UpdateSphereHeightFieldConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject< FHeightField >(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::SpherePlane:
				UpdateSpherePlaneConstraint(*Implicit0.template GetObject<TSphere<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<TPlane<FReal, 3>>(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::CapsuleCapsule:
				UpdateCapsuleCapsuleConstraint(*Implicit0.template GetObject<FCapsule>(), WorldTransform0, *Implicit1.template GetObject<FCapsule>(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::CapsuleBox:
				UpdateCapsuleBoxConstraint(*Implicit0.template GetObject<FCapsule>(), WorldTransform0, *Implicit1.template GetObject<TBox<FReal, 3>>(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::CapsuleConvex:
				UpdateCapsuleConvexConstraint(*Implicit0.template GetObject<FCapsule>(), WorldTransform0, Implicit1, WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::CapsuleTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateCapsuleTriangleMeshConstraint(*Implicit0.template GetObject<FCapsule>(), WorldTransform0, *ScaledTriMesh, WorldTransform1, Dt, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateCapsuleTriangleMeshConstraint(*Implicit0.template GetObject<FCapsule>(), WorldTransform0, *TriangleMeshImplicit, WorldTransform1, Dt, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::CapsuleHeightField:
				UpdateCapsuleHeightFieldConstraint(*Implicit0.template GetObject<FCapsule>(), WorldTransform0, *Implicit1.template GetObject< FHeightField >(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::BoxBox:
				UpdateBoxBoxConstraint(*Implicit0.template GetObject<TBox<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<TBox<FReal, 3>>(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::BoxConvex:
				UpdateGenericConvexConvexConstraint(Implicit0, WorldTransform0, Implicit1, WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::BoxTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateBoxTriangleMeshConstraint(*Implicit0.template GetObject<TBox<FReal, 3>>(), WorldTransform0, *ScaledTriMesh, WorldTransform1, Dt, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateBoxTriangleMeshConstraint(*Implicit0.template GetObject<TBox<FReal, 3>>(), WorldTransform0, *TriangleMeshImplicit, WorldTransform1, Dt, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::BoxHeightField:
				UpdateBoxHeightFieldConstraint(*Implicit0.template GetObject<TBox<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject< FHeightField >(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::BoxPlane:
				UpdateBoxPlaneConstraint(*Implicit0.template GetObject<TBox<FReal, 3>>(), WorldTransform0, *Implicit1.template GetObject<TPlane<FReal, 3>>(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::GenericConvexConvex:
				UpdateGenericConvexConvexConstraint(Implicit0, WorldTransform0, Implicit1, WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::ConvexTriMesh:
				if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject> >())
				{
					UpdateConvexTriangleMeshConstraint(Implicit0, WorldTransform0, *ScaledTriMesh, WorldTransform1, Dt, Constraint);
					break;
				}
				else if (const FTriangleMeshImplicitObject* TriangleMeshImplicit = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
				{
					UpdateConvexTriangleMeshConstraint(Implicit0, WorldTransform0, *TriangleMeshImplicit, WorldTransform1, Dt, Constraint);
					break;
				}
				ensure(false);
				break;
			case EContactShapesType::ConvexHeightField:
				UpdateConvexHeightFieldConstraint(Implicit0, WorldTransform0, *Implicit1.template GetObject< FHeightField >(), WorldTransform1, Dt, Constraint);
				break;
			case EContactShapesType::LevelSetLevelSet:
				UpdateLevelsetLevelsetConstraint<UpdateType>(WorldTransform0, WorldTransform1, Dt, Constraint);
				break;
			default:
				// Switch needs updating....
				ensure(false);
				break;
			}

			//const FVec3 NewContactPositionLocal0 = WorldTransform0.InverseTransformPosition(Constraint.Manifold.Location);
			//const FVec3 NewContactPositionLocal1 = WorldTransform1.InverseTransformPosition(Constraint.Manifold.Location);
			//Constraint.Manifold.ContactMoveSQRDistance = FMath::Max((NewContactPositionLocal0 - OriginalContactPositionLocal0).SizeSquared(), (NewContactPositionLocal1 - OriginalContactPositionLocal1).SizeSquared());
		}

		// Run collision detection for the swept constraints
		// NOTE: Transforms are world space shape transforms
		
		template<ECollisionUpdateType UpdateType>
		inline void UpdateConstraintFromGeometryImpl(FRigidBodySweptPointContactConstraint& Constraint, const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal Dt)
		{
			FReal LengthCCD = 0.0f;
			FVec3 DirCCD(0.0f);
			bool bUseCCD = UseCCD(Constraint.Particle[0], Constraint.Particle[1], Constraint.GetManifold().Implicit[0], DirCCD, LengthCCD);

			const FImplicitObject& Implicit0 = *Constraint.Manifold.Implicit[0];
			const FImplicitObject& Implicit1 = *Constraint.Manifold.Implicit[1];

			TGeometryParticleHandle<FReal, 3>* Particle0 = Constraint.Particle[0];

			if (bUseCCD)
			{
				switch (Constraint.Manifold.ShapesType)
				{
				case EContactShapesType::GenericConvexConvex:
					UpdateGenericConvexConvexConstraintSwept(Particle0, Implicit0, WorldTransform0, Implicit1, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					return;
				case EContactShapesType::SphereHeightField:
				{
					const TSphere<FReal, 3>* Object0 = Implicit0.template GetObject<const TSphere<FReal, 3>>();
					const FHeightField* Object1 = Implicit1.template GetObject<const FHeightField>();
					UpdateSphereHeightFieldConstraintSwept(Particle0, *Object0, WorldTransform0, *Object1, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					return;
				}
				case EContactShapesType::CapsuleHeightField:
				{
					const FCapsule* Object0 = Implicit0.template GetObject<const FCapsule >();
					const FHeightField* Object1 = Implicit1.template GetObject<const FHeightField >();
					UpdateCapsuleHeightFieldConstraintSwept(Particle0, *Object0, WorldTransform0, *Object1, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					return;
				}
				case EContactShapesType::SphereTriMesh:
				{
					const TSphere<FReal, 3>* Object0 = Implicit0.template GetObject<const TSphere<FReal, 3>>();
					if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
					{
						UpdateSphereTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					}
					else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
					{
						UpdateSphereTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransform0, *TriangleMesh, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					}
					else
					{
						ensure(false);
					}
					return;
				}
				case EContactShapesType::CapsuleTriMesh:
				{
					const FCapsule* Object0 = Implicit0.template GetObject<const FCapsule >();
					if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
					{
						UpdateCapsuleTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					}
					else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
					{
						UpdateCapsuleTriangleMeshConstraintSwept(Particle0, *Object0, WorldTransform0, *TriangleMesh, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					}
					else
					{
						ensure(false);
					}
					return;
				}
				case EContactShapesType::ConvexHeightField:
				{
					const FHeightField* Object1 = Implicit1.template GetObject<const FHeightField >();
					UpdateConvexHeightFieldConstraintSwept(Particle0, Implicit0, WorldTransform0, *Object1, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					return;
				}
				case EContactShapesType::ConvexTriMesh:
					if (const TImplicitObjectScaled<FTriangleMeshImplicitObject>* ScaledTriangleMesh = Implicit1.template GetObject<const TImplicitObjectScaled<FTriangleMeshImplicitObject>>())
					{
						UpdateConvexTriangleMeshConstraintSwept(Particle0, Implicit0, WorldTransform0, *ScaledTriangleMesh, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					}
					else if (const FTriangleMeshImplicitObject* TriangleMesh = Implicit1.template GetObject<const FTriangleMeshImplicitObject>())
					{
						UpdateConvexTriangleMeshConstraintSwept(Particle0, Implicit0, WorldTransform0, *TriangleMesh, WorldTransform1, DirCCD, LengthCCD, Dt, Constraint);
					}
					else
					{
						ensure(false);
					}
					return;
				}
			}

			Constraint.TimeOfImpact = 1.0f; // CCD will not be used
			// Do a normal non-swept update if we reach this point - Not required for now, since this will be done in solver
			// UpdateConstraintFromGeometryImpl<UpdateType>(*(Constraint.As<FRigidBodyPointContactConstraint>()), WorldTransform0, WorldTransform1, Dt);
		}

		template<typename T_TRAITS>
		void ConstructConstraintsImpl(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructConstraintsInternal, ConstraintsDetailedStats);

			// @todo(chaos): We use GetInnerType here because TriMeshes are left with their "Instanced" wrapper, unlike all other instanced implicits. Should we strip the instance on Tri Mesh too?
			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetCollisionType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetCollisionType()) : ImplicitObjectType::Unknown;
			bool bIsConvex0 = Implicit0 && Implicit0->IsConvex() && Implicit0Type != ImplicitObjectType::LevelSet;
			bool bIsConvex1 = Implicit1 && Implicit1->IsConvex() && Implicit1Type != ImplicitObjectType::LevelSet;

			FReal LengthCCD = 0.0f;
			FVec3 DirCCD(0.0f);
			bool bUseCCD = UseCCD(Particle0, Particle1, Implicit0, DirCCD, LengthCCD);

#if CHAOS_COLLISION_CREATE_BOUNDSCHECK
			if ((Implicit0 != nullptr) && (Implicit1 != nullptr))
			{
				if (Implicit0->HasBoundingBox() && Implicit1->HasBoundingBox())
				{
					const FRigidTransform3 ParticleTransform0 = Collisions::GetTransform(Particle0);
					const FRigidTransform3 ParticleTransform1 = Collisions::GetTransform(Particle1);
					const FRigidTransform3 WorldTransform0 = LocalTransform0 * ParticleTransform0;
					const FRigidTransform3 WorldTransform1 = LocalTransform1 * ParticleTransform1;
					if (Chaos_Collision_NarrowPhase_SphereBoundsCheck)
					{
						const FReal R1 = Implicit0->BoundingBox().OriginRadius();
						const FReal R2 = Implicit1->BoundingBox().OriginRadius();
						const FReal SeparationSq = (WorldTransform1.GetTranslation() - WorldTransform0.GetTranslation()).SizeSquared();
						if (SeparationSq > FMath::Square(R1 + R2 + CullDistance))
						{
							return;
						}
					}

					if (Chaos_Collision_NarrowPhase_AABBBoundsCheck)
					{
						const FRigidTransform3 Box2ToBox1TM = WorldTransform1.GetRelativeTransform(WorldTransform0);
						const FAABB3 Box1 = Implicit0->BoundingBox();
						const FAABB3 Box2In1 = Implicit1->BoundingBox().TransformedAABB(Box2ToBox1TM).Thicken(CullDistance);
						if (!Box1.Intersects(Box2In1))
						{
							return;
						}
					}
				}
			}
#endif

			if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxBoxConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				ConstructBoxHeightFieldConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FHeightField::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxHeightFieldConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TPlane<FReal, 3>::StaticType())
			{
				ConstructBoxPlaneConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TPlane<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxPlaneConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereSphereConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				if (bUseCCD)
				{
					ConstructSphereHeightFieldConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}
				ConstructSphereHeightFieldConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FHeightField::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				if (bUseCCD)
				{
					ConstructSphereHeightFieldConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}
				ConstructSphereHeightFieldConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TPlane<FReal, 3>::StaticType())
			{
				ConstructSpherePlaneConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TPlane<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSpherePlaneConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructSphereBoxConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereBoxConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FCapsule::StaticType())
			{
				ConstructSphereCapsuleConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FCapsule::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereCapsuleConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FImplicitConvex3::StaticType())
			{
				ConstructSphereConvexConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FImplicitConvex3::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				ConstructSphereConvexConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FCapsule::StaticType() && Implicit1Type == FCapsule::StaticType())
			{
				ConstructCapsuleCapsuleConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FCapsule::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructCapsuleBoxConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == FCapsule::StaticType())
			{
				ConstructCapsuleBoxConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FCapsule::StaticType() && Implicit1Type == FImplicitConvex3::StaticType())
			{
				ConstructCapsuleConvexConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FImplicitConvex3::StaticType() && Implicit1Type == FCapsule::StaticType())
			{
				ConstructCapsuleConvexConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FCapsule::StaticType() && Implicit1Type == FHeightField::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleHeightFieldConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleHeightFieldConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FHeightField::StaticType() && Implicit1Type == FCapsule::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleHeightFieldConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleHeightFieldConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TBox<FReal, 3>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				ConstructBoxTriangleMeshConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TBox<FReal, 3>::StaticType())
			{
				ConstructBoxTriangleMeshConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == TSphere<FReal, 3>::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				if (bUseCCD)
				{
					ConstructSphereTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}
				ConstructSphereTriangleMeshConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == TSphere<FReal, 3>::StaticType())
			{
				if (bUseCCD)
				{
					ConstructSphereTriangleMeshConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}
				ConstructSphereTriangleMeshConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FCapsule::StaticType() && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleTriangleMeshConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && Implicit1Type == FCapsule::StaticType())
			{
				if (bUseCCD)
				{
					ConstructCapsuleTriangleMeshConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructCapsuleTriangleMeshConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (bIsConvex0 && Implicit1Type == FHeightField::StaticType())
			{
				if (bUseCCD)
				{
					ConstructConvexHeightFieldConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexHeightFieldConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FHeightField::StaticType() && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructConvexHeightFieldConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexHeightFieldConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (bIsConvex0 && Implicit1Type == FTriangleMeshImplicitObject::StaticType())
			{
				if (bUseCCD)
				{
					ConstructConvexTriangleMeshConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexTriangleMeshConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else if (Implicit0Type == FTriangleMeshImplicitObject::StaticType() && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructConvexTriangleMeshConstraintsSwept(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructConvexTriangleMeshConstraints<T_TRAITS>(Particle1, Particle0, Implicit1, Implicit0, LocalTransform1, LocalTransform0, CullDistance, Dt, Context, NewConstraints);
			}
			else if (bIsConvex0 && bIsConvex1)
			{
				if (bUseCCD)
				{
					ConstructGenericConvexConvexConstraintsSwept(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, DirCCD, LengthCCD, NewConstraints);
					return;
				}

				ConstructGenericConvexConvexConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Implicit1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
			else
			{
				ConstructLevelsetLevelsetConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Simplicial0, Implicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
			}
		}


		// Run collision detection for the specified constraint to update the nearest contact point.
		// NOTE: Transforms are world space particle transforms
		template<ECollisionUpdateType UpdateType, typename RigidBodyContactConstraint>
		void UpdateConstraintFromGeometry(RigidBodyContactConstraint& Constraint, const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal Dt)
		{
			const FRigidTransform3 WorldTransform0 = Constraint.ImplicitTransform[0] * ParticleTransform0;
			const FRigidTransform3 WorldTransform1 = Constraint.ImplicitTransform[1] * ParticleTransform1;
			UpdateConstraintFromGeometryImpl<UpdateType>(Constraint, WorldTransform0, WorldTransform1, Dt);
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

		inline bool HasSimEnabled(const FPerShapeData* Shape)
		{
			return (!Shape || (Shape->GetSimEnabled() && IsValid(Shape->GetSimData())));
		}

		inline bool DoCollide(EImplicitObjectType Implicit0Type, const FPerShapeData* Shape0, EImplicitObjectType Implicit1Type, const FPerShapeData* Shape1)
		{
			//
			// Disabled shapes do not collide
			//
			if (!HasSimEnabled(Shape0)) return false;
			if (!HasSimEnabled(Shape1)) return false;

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
		void ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, FReal Dt, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_ConstructConstraints, ConstraintsDetailedStats);

			EImplicitObjectType Implicit0Type = Implicit0 ? GetInnerType(Implicit0->GetType()) : ImplicitObjectType::Unknown;
			EImplicitObjectType Implicit1Type = Implicit1 ? GetInnerType(Implicit1->GetType()) : ImplicitObjectType::Unknown;


			if (!Implicit0 || !Implicit1)
			{
				ConstructLevelsetLevelsetConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Simplicial0, Implicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
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
				ConstructConstraints<T_TRAITS>(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), Simplicial0, TransformedImplicit1->GetTransformedObject(), Simplicial1, TransformedTransform0, TransformedTransform1, CullDistance, Dt, Context, NewConstraints);
				return;
			}
			else if (Implicit0OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit0 = Implicit0->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				FRigidTransform3 TransformedTransform0 = TransformedImplicit0->GetTransform() * LocalTransform0;
				ConstructConstraints<T_TRAITS>(Particle0, Particle1, TransformedImplicit0->GetTransformedObject(), Simplicial0, Implicit1, Simplicial1, TransformedTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
				return;
			}
			else if (Implicit1OuterType == TImplicitObjectTransformed<FReal, 3>::StaticType())
			{
				const TImplicitObjectTransformed<FReal, 3>* TransformedImplicit1 = Implicit1->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
				FRigidTransform3 TransformedTransform1 = TransformedImplicit1->GetTransform() * LocalTransform1;
				ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Simplicial0, TransformedImplicit1->GetTransformedObject(), Simplicial1, LocalTransform0, TransformedTransform1, CullDistance, Dt, Context, NewConstraints);
				return;
			}

			// Strip the Instanced wrapper from most shapes, but not Convex or TriMesh.
			// Convex collision requires the wrapper because it holds the margin.
			// @todo(chaos): this collision logic is getting out of hand - can we make a better shape class hierarchy?
			if (((uint32)Implicit0OuterType & ImplicitObjectType::IsInstanced) || ((uint32)Implicit1OuterType & ImplicitObjectType::IsInstanced))
			{
				const FImplicitObject* InnerImplicit0 = nullptr;
				const FImplicitObject* InnerImplicit1 = nullptr;
				if (((uint32)Implicit0OuterType & ImplicitObjectType::IsInstanced) && (Implicit0Type != FImplicitConvex3::StaticType()))
				{
					InnerImplicit0 = GetInstancedImplicit(Implicit0);
				}
				if (((uint32)Implicit1OuterType & ImplicitObjectType::IsInstanced) && (Implicit1Type != FImplicitConvex3::StaticType()))
				{
					InnerImplicit1 = GetInstancedImplicit(Implicit1);
				}
				if (InnerImplicit0 && InnerImplicit1)
				{
					ConstructConstraints<T_TRAITS>(Particle0, Particle1, InnerImplicit0, Simplicial0, InnerImplicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					return;
				}
				else if (InnerImplicit0 && !InnerImplicit1)
				{
					ConstructConstraints<T_TRAITS>(Particle0, Particle1, InnerImplicit0, Simplicial0, Implicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					return;
				}
				else if (!InnerImplicit0 && InnerImplicit1)
				{
					ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Simplicial0, InnerImplicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					return;
				}

			}

			// Handle Unions
			if (Implicit0OuterType == FImplicitObjectUnion::StaticType())
			{
				const FImplicitObjectUnion* Union0 = Implicit0->template GetObject<FImplicitObjectUnion>();
				for (const auto& Child0 : Union0->GetObjects())
				{
					// If shape is not sim'd, we may end up iterating over a lot of shapes on particle1's union and wasting time filtering.
					if (Context.bFilteringEnabled == false || HasSimEnabled(Particle0->GetImplicitShape(Child0.Get())))
					{
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Child0.Get(), Simplicial0, Implicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					}
				}
				return;
			}

			if (Implicit0OuterType == FImplicitObjectUnionClustered::StaticType())
			{
				const FImplicitObjectUnionClustered* Union0 = Implicit0->template GetObject<FImplicitObjectUnionClustered>();
				if (Implicit1->HasBoundingBox())
				{
					TArray<Pair<Pair<const FImplicitObject*, const FBVHParticles*>, FRigidTransform3>> Children;

					// Need to get transformed bounds of 1 in the space of 0
					FRigidTransform3 TM0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 TM1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					FRigidTransform3 TM1ToTM0 = TM1.GetRelativeTransform(TM0);
					FAABB3 QueryBounds = Implicit1->BoundingBox().TransformedAABB(TM1ToTM0);

					Union0->FindAllIntersectingClusteredObjects(Children, QueryBounds);

					for (const Pair<Pair<const FImplicitObject*, const FBVHParticles*>, FRigidTransform3>& Child0 : Children)
					{
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Child0.First.First, Child0.First.Second, Implicit1, Simplicial1, Child0.Second * LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					}
				}
				else
				{
					for (const TUniquePtr<FImplicitObject>& Child0 : Union0->GetObjects())
					{
						const TPBDRigidParticleHandle<FReal, 3>* OriginalParticle = Union0->FindParticleForImplicitObject(Child0.Get());
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Child0.Get(), OriginalParticle ? OriginalParticle->CollisionParticles().Get() : Simplicial0, Implicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					}
				}
				return;
			}

			if (Implicit1OuterType == FImplicitObjectUnion::StaticType())
			{
				const FImplicitObjectUnion* Union1 = Implicit1->template GetObject<FImplicitObjectUnion>();
				for (const auto& Child1 : Union1->GetObjects())
				{
					// If shape is not sim'd, we may end up iterating over a lot of shapes on particle1's union and wasting time filtering.
					if (Context.bFilteringEnabled == false || HasSimEnabled(Particle1->GetImplicitShape(Child1.Get())))
					{
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Simplicial0, Child1.Get(), Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					}
				}
				return;
			}

			if (Implicit1OuterType == FImplicitObjectUnionClustered::StaticType())
			{
				const FImplicitObjectUnionClustered* Union1 = Implicit1->template GetObject<FImplicitObjectUnionClustered>();
				if (Implicit0->HasBoundingBox())
				{
					TArray<Pair<Pair<const FImplicitObject*, const FBVHParticles*>, FRigidTransform3>> Children;

					// Need to get transformed bounds of 0 in the space of 1
					FRigidTransform3 TM0 = LocalTransform0 * Collisions::GetTransform(Particle0);
					FRigidTransform3 TM1 = LocalTransform1 * Collisions::GetTransform(Particle1);
					FRigidTransform3 TM0ToTM1 = TM0.GetRelativeTransform(TM1);
					FAABB3 QueryBounds = Implicit0->BoundingBox().TransformedAABB(TM0ToTM1);

					{
						CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_Collisions_FindAllIntersectingClusteredObjects, ConstraintsDetailedStats);
						Union1->FindAllIntersectingClusteredObjects(Children, QueryBounds);
					}

					for (const Pair<Pair<const FImplicitObject*, const FBVHParticles*>, FRigidTransform3>& Child1 : Children)
					{
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Simplicial0, Child1.First.First, Child1.First.Second, LocalTransform0, Child1.Second * LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					}
				}
				else
				{
					for (const TUniquePtr<FImplicitObject>& Child1 : Union1->GetObjects())
					{
						const TPBDRigidParticleHandle<FReal, 3>* OriginalParticle = Union1->FindParticleForImplicitObject(Child1.Get());
						ConstructConstraints<T_TRAITS>(Particle0, Particle1, Implicit0, Simplicial0, Child1.Get(), OriginalParticle ? OriginalParticle->CollisionParticles().Get() : Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
					}
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
			ConstructConstraintsImpl<T_TRAITS>(Particle0, Particle1, Implicit0, Simplicial0, Implicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, Dt, Context, NewConstraints);
		}

		void ConstructConstraints(TGeometryParticleHandle<FReal, 3>* Particle0, TGeometryParticleHandle<FReal, 3>* Particle1, const FImplicitObject* Implicit0, const FBVHParticles* Simplicial0, const FImplicitObject* Implicit1, const FBVHParticles* Simplicial1, const FRigidTransform3& LocalTransform0, const FRigidTransform3& LocalTransform1, const FReal CullDistance, const FReal dT, const FCollisionContext& Context, FCollisionConstraintsArray& NewConstraints)
		{
			bool bDeferUpdate = Context.bDeferUpdate;
			// Skip constraint update for sleeping particles
			if(Particle0 && Particle1)
			{
				TPBDRigidParticleHandle<FReal, 3>* RigidParticle0 = Particle0->CastToRigidParticle(), *RigidParticle1 = Particle1->CastToRigidParticle();
				if(RigidParticle0 && RigidParticle1)
				{
					bDeferUpdate |= (RigidParticle0->ObjectState() == EObjectStateType::Sleeping) || (RigidParticle1->ObjectState() == EObjectStateType::Sleeping);
				}
			}
			if (bDeferUpdate)
			{
				using TTraits = TConstructCollisionTraits<false>;
				ConstructConstraints<TTraits>(Particle0, Particle1, Implicit0, Simplicial0, Implicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, dT, Context, NewConstraints);
			}
			else
			{
				using TTraits = TConstructCollisionTraits<true>;
				ConstructConstraints<TTraits>(Particle0, Particle1, Implicit0, Simplicial0, Implicit1, Simplicial1, LocalTransform0, LocalTransform1, CullDistance, dT, Context, NewConstraints);
			}
		}


		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Any>(const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint);
		template void UpdateLevelsetLevelsetConstraint<ECollisionUpdateType::Deepest>(const FRigidTransform3& WorldTransform0, const FRigidTransform3& WorldTransform1, const FReal Dt, FRigidBodyPointContactConstraint& Constraint);

		template void UpdateConstraintFromGeometry<ECollisionUpdateType::Any, FRigidBodyPointContactConstraint>(FRigidBodyPointContactConstraint& ConstraintBase, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal Dt);
		template void UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest, FRigidBodyPointContactConstraint>(FRigidBodyPointContactConstraint& ConstraintBase, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal Dt);

		template void UpdateConstraintFromGeometry<ECollisionUpdateType::Any, FRigidBodySweptPointContactConstraint>(FRigidBodySweptPointContactConstraint& ConstraintBase, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal Dt);
		template void UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest, FRigidBodySweptPointContactConstraint>(FRigidBodySweptPointContactConstraint& ConstraintBase, const FRigidTransform3& Transform0, const FRigidTransform3& Transform1, const FReal Dt);

	} // Collisions


} // Chaos
