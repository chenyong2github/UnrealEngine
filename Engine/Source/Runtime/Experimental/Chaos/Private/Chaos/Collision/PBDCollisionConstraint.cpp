// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Collision/CollisionConstraintAllocator.h"
#include "Chaos/Collision/PBDCollisionConstraintHandle.h"
#include "Chaos/Collision/SolverCollisionContainer.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/Evolution/SolverBody.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"

// Private includes
#include "PBDCollisionSolver.h"


//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	FRealSingle Chaos_Manifold_MatchPositionTolerance = 0.3f;		// Fraction of object size position tolerance
	FRealSingle Chaos_Manifold_MatchNormalTolerance = 0.02f;		// Dot product tolerance
	FAutoConsoleVariableRef CVarChaos_Manifold_MatchPositionTolerance(TEXT("p.Chaos.Collision.Manifold.MatchPositionTolerance"), Chaos_Manifold_MatchPositionTolerance, TEXT("A tolerance as a fraction of object size used to determine if two contact points are the same"));
	FAutoConsoleVariableRef CVarChaos_Manifold_MatchNormalTolerance(TEXT("p.Chaos.Collision.Manifold.MatchNormalTolerance"), Chaos_Manifold_MatchNormalTolerance, TEXT("A tolerance on the normal dot product used to determine if two contact points are the same"));

	FRealSingle Chaos_Manifold_FrictionPositionTolerance = 1.0f;	// Distance a shape-relative contact point can move and still be considered the same point
	FAutoConsoleVariableRef CVarChaos_Manifold_FrictionPositionTolerance(TEXT("p.Chaos.Collision.Manifold.FrictionPositionTolerance"), Chaos_Manifold_FrictionPositionTolerance, TEXT(""));

	FRealSingle Chaos_GBFCharacteristicTimeRatio = 1.0f;
	FAutoConsoleVariableRef CVarChaos_GBFCharacteristicTimeRatio(TEXT("p.Chaos.Collision.GBFCharacteristicTimeRatio"), Chaos_GBFCharacteristicTimeRatio, TEXT("The ratio between characteristic time and Dt"));

	bool bChaos_Manifold_EnabledWithJoints = true;
	FAutoConsoleVariableRef CVarChaos_Manifold_EnabledWithJoints(TEXT("p.Chaos.Collision.Manifold.EnabledWithJoints"), bChaos_Manifold_EnabledWithJoints, TEXT(""));

	bool bChaos_Manifold_EnableGjkWarmStart = true;
	FAutoConsoleVariableRef CVarChaos_Manifold_EnableGjkWarmStart(TEXT("p.Chaos.Collision.Manifold.EnableGjkWarmStart"), bChaos_Manifold_EnableGjkWarmStart, TEXT(""));

	bool bChaos_Manifold_EnableFrictionRestore = true;
	FAutoConsoleVariableRef CVarChaos_Manifold_EnableFrictionRestore(TEXT("p.Chaos.Collision.Manifold.EnableFrictionRestore"), bChaos_Manifold_EnableFrictionRestore, TEXT(""));

	extern bool bChaos_Collision_Manifold_FixNormalsInWorldSpace;

	FString FPBDCollisionConstraint::ToString() const
	{
		return FString::Printf(TEXT("Particle:%s, Levelset:%s, AccumulatedImpulse:%s"), *Particle[0]->ToString(), *Particle[1]->ToString(), *AccumulatedImpulse.ToString());
	}

	bool ContactConstraintSortPredicate(const FPBDCollisionConstraint& L, const FPBDCollisionConstraint& R)
	{
		//sort constraints by the smallest particle idx in them first
		//if the smallest particle idx is the same for both, use the other idx

		if (L.GetCCDType() != R.GetCCDType())
		{
			return L.GetCCDType() < R.GetCCDType();
		}

		const FParticleID ParticleIdxs[] = { L.Particle[0]->ParticleID(), L.Particle[1]->ParticleID() };
		const FParticleID OtherParticleIdxs[] = { R.Particle[0]->ParticleID(), R.Particle[1]->ParticleID() };

		const int32 MinIdx = ParticleIdxs[0] < ParticleIdxs[1] ? 0 : 1;
		const int32 OtherMinIdx = OtherParticleIdxs[0] < OtherParticleIdxs[1] ? 0 : 1;

		if(ParticleIdxs[MinIdx] < OtherParticleIdxs[OtherMinIdx])
		{
			return true;
		} 
		else if(ParticleIdxs[MinIdx] == OtherParticleIdxs[OtherMinIdx])
		{
			return ParticleIdxs[!MinIdx] < OtherParticleIdxs[!OtherMinIdx];
		}

		return false;
	}

	TUniquePtr<FPBDCollisionConstraint> FPBDCollisionConstraint::Make(
		FGeometryParticleHandle* Particle0,
		const FImplicitObject* Implicit0,
		const FBVHParticles* Simplicial0,
		const FRigidTransform3& ImplicitLocalTransform0,
		FGeometryParticleHandle* Particle1,
		const FImplicitObject* Implicit1,
		const FBVHParticles* Simplicial1,
		const FRigidTransform3& ImplicitLocalTransform1,
		const FReal InCullDistance,
		const bool bInUseManifold,
		const EContactShapesType ShapesType)
	{
		FPBDCollisionConstraint* Constraint = new FPBDCollisionConstraint(Particle0, Implicit0, Simplicial0, Particle1, Implicit1, Simplicial1);
		
		Constraint->Setup(ECollisionCCDType::Disabled, ShapesType, ImplicitLocalTransform0, ImplicitLocalTransform1, InCullDistance, bInUseManifold);

		return TUniquePtr<FPBDCollisionConstraint>(Constraint);
	}

	FPBDCollisionConstraint FPBDCollisionConstraint::MakeCopy(
		const FPBDCollisionConstraint& Source)
	{
		// @todo(chaos): The resim cache version probably doesn't need all the data, so maybe try to cur this down?
		FPBDCollisionConstraint Constraint = Source;

		// Invalidate the data that maps the constraint to its container (we are no longer in the container)
		// @todo(chaos): this should probably be handled by the copy constructor
		Constraint.GetContainerCookie().ClearContainerData();

		return Constraint;
	}

	FPBDCollisionConstraint::FPBDCollisionConstraint()
		: ImplicitTransform{ FRigidTransform3(), FRigidTransform3() }
		, Particle{ nullptr, nullptr }
		, AccumulatedImpulse(0)
		, Manifold()
		, TimeOfImpact(0)
		, CCDType(ECollisionCCDType::Disabled)
		, Stiffness(FReal(1))
		, CullDistance(TNumericLimits<FReal>::Max())
		, bUseManifold(false)
		, bUseIncrementalManifold(false)
		, SolverBodies{ nullptr, nullptr }
		, GJKWarmStartData()
		, PrevManifoldPoints()
		, bWasManifoldRestored(false)
		, NumActivePositionIterations(0)
	{
		Manifold.Implicit[0] = nullptr;
		Manifold.Implicit[1] = nullptr;
		Manifold.Simplicial[0] = nullptr;
		Manifold.Simplicial[1] = nullptr;
		Manifold.ShapesType = EContactShapesType::Unknown;
	}

	FPBDCollisionConstraint::FPBDCollisionConstraint(
		FGeometryParticleHandle* Particle0,
		const FImplicitObject* Implicit0,
		const FBVHParticles* Simplicial0,
		FGeometryParticleHandle* Particle1,
		const FImplicitObject* Implicit1,
		const FBVHParticles* Simplicial1)
		: ImplicitTransform{ FRigidTransform3(), FRigidTransform3() }
		, Particle{ Particle0, Particle1 }
		, AccumulatedImpulse(0)
		, Manifold()
		, TimeOfImpact(0)
		, CCDType(ECollisionCCDType::Disabled)
		, Stiffness(FReal(1))
		, CullDistance(TNumericLimits<FReal>::Max())
		, bUseManifold(false)
		, bUseIncrementalManifold(false)
		, SolverBodies{ nullptr, nullptr }
		, GJKWarmStartData()
		, PrevManifoldPoints()
		, bWasManifoldRestored(false)
	{
		Manifold.Implicit[0] = Implicit0;
		Manifold.Implicit[1] = Implicit1;
		Manifold.Simplicial[0] = Simplicial0;
		Manifold.Simplicial[1] = Simplicial1;
		Manifold.ShapesType = EContactShapesType::Unknown;
	}

	void FPBDCollisionConstraint::Setup(
		const ECollisionCCDType InCCDType,
		const EContactShapesType InShapesType,
		const FRigidTransform3& InImplicitLocalTransform0,
		const FRigidTransform3& InImplicitLocalTransform1,
		const FReal InCullDistance,
		const bool bInUseManifold)
	{
		CCDType = InCCDType;

		Manifold.ShapesType = InShapesType;

		ImplicitTransform[0] = InImplicitLocalTransform0;
		ImplicitTransform[1] = InImplicitLocalTransform1;

		CullDistance = InCullDistance;

		bUseManifold = bInUseManifold && CanUseManifold(Particle[0], Particle[1]);
		bUseIncrementalManifold = true;	// This will get changed later if we call AddOneShotManifoldContact
	}

	void FPBDCollisionConstraint::SetIsSleeping(const bool bInIsSleeping)
	{
		// This actually sets the sleeping state on all constraints between the same particle pair so calling this with multiple
		// constraints on the same particle pair is a little wasteful. It early-outs on subsequent calls, but still not ideal.
		// @todo(chaos): we only need to set sleeping on particle pairs or particles, not constraints (See UpdateSleepState in IslandManager.cpp)
		check(ContainerCookie.MidPhase != nullptr);
		ContainerCookie.MidPhase->SetIsSleeping(bInIsSleeping);
	}

	// Are the two manifold points the same point?
	// Ideally a contact is considered the same as one from the previous iteration if
	//		The contact is Vertex - Face and there was a prior iteration collision on the same Vertex
	//		The contact is Edge - Edge and a prior iteration collision contained both edges
	//		The contact is Face - Face and a prior iteration contained both faces
	//
	// But we don’t have feature IDs. So in the meantime contact points will be considered the "same" if
	//		Vertex - Face - the local space contact position on either body is within some tolerance
	//		Edge - Edge - ?? hard...
	//		Face - Face - ?? hard...
	//
	bool FPBDCollisionConstraint::AreMatchingContactPoints(const FContactPoint& A, const FContactPoint& B, FReal& OutScore) const
	{
		OutScore = 0.0f;

		// @todo(chaos): cache tolerances?
		FReal DistanceTolerance = 0.0f;
		if (Particle[0]->Geometry()->HasBoundingBox() && Particle[1]->Geometry()->HasBoundingBox())
		{
			const FReal Size0 = Particle[0]->Geometry()->BoundingBox().Extents().Max();
			const FReal Size1 = Particle[1]->Geometry()->BoundingBox().Extents().Max();
			DistanceTolerance = FMath::Min(Size0, Size1) * Chaos_Manifold_MatchPositionTolerance;
		}
		else if (Particle[0]->Geometry()->HasBoundingBox())
		{
			const FReal Size0 = Particle[0]->Geometry()->BoundingBox().Extents().Max();
			DistanceTolerance = Size0 * Chaos_Manifold_MatchPositionTolerance;
		}
		else if (Particle[1]->Geometry()->HasBoundingBox())
		{
			const FReal Size1 = Particle[1]->Geometry()->BoundingBox().Extents().Max();
			DistanceTolerance = Size1 * Chaos_Manifold_MatchPositionTolerance;
		}
		else
		{
			return false;
		}
		const FReal NormalTolerance = Chaos_Manifold_MatchNormalTolerance;

		// If normal has changed a lot, it is a different contact
		// (This was only here to detect bad normals - it is not right for edge-edge contact tracking, but we don't do a good job of that yet anyway!)
		FReal NormalDot = FVec3::DotProduct(A.ShapeContactNormal, B.ShapeContactNormal);
		if (NormalDot < 1.0f - NormalTolerance)
		{
			return false;
		}

		// If either point in local space is the same, it is the same contact
		if (DistanceTolerance > 0.0f)
		{
			const FReal DistanceTolerance2 = DistanceTolerance * DistanceTolerance;
			for (int32 BodyIndex = 0; BodyIndex < 2; ++BodyIndex)
			{
				FVec3 DR = A.ShapeContactPoints[BodyIndex] - B.ShapeContactPoints[BodyIndex];
				FReal DRLen2 = DR.SizeSquared();
				if (DRLen2 < DistanceTolerance2)
				{
					OutScore = FMath::Clamp(1.f - DRLen2 / DistanceTolerance2, 0.f, 1.f);
					return true;
				}
			}
		}

		return false;
	}

	int32 FPBDCollisionConstraint::FindManifoldPoint(const FContactPoint& ContactPoint) const
	{
		const int32 NumManifoldPoints = ManifoldPoints.Num();
		int32 BestMatchIndex = INDEX_NONE;
		FReal BestMatchScore = 0.0f;
		for (int32 ManifoldPointIndex = 0; ManifoldPointIndex < NumManifoldPoints; ++ManifoldPointIndex)
		{
			FReal Score = 0.0f;
			if (AreMatchingContactPoints(ContactPoint, ManifoldPoints[ManifoldPointIndex].ContactPoint, Score))
			{
				if (Score > BestMatchScore)
				{
					BestMatchIndex = ManifoldPointIndex;
					BestMatchScore = Score;

					// Just take the first one that meets the tolerances
					break;
				}
			}
		}
		return BestMatchIndex;
	}

	// @todo(chaos): this exists primarily for the contact modification callbacks. See if we can eliminate it when not needed
	void FPBDCollisionConstraint::UpdateManifoldContacts()
	{
		FVec3 P0, P1;
		FRotation3 Q0, Q1;

		// @todo(chaos): Remove this when we don't need to support incremental manifolds (this will only be called on creation/restore)
		if ((GetSolverBody0() != nullptr) && (GetSolverBody1() != nullptr))
		{
			P0 = GetSolverBody0()->P();
			Q0 = GetSolverBody0()->Q();
			P1 = GetSolverBody1()->P();
			Q1 = GetSolverBody1()->Q();
		}
		else
		{
			// @todo(chaos): we should not need to regenerate the CoM transform
			P0 = FParticleUtilities::GetCoMWorldPosition(FConstGenericParticleHandle(Particle[0]));
			Q0 = FParticleUtilities::GetCoMWorldRotation(FConstGenericParticleHandle(Particle[0]));
			P1 = FParticleUtilities::GetCoMWorldPosition(FConstGenericParticleHandle(Particle[1]));
			Q1 = FParticleUtilities::GetCoMWorldRotation(FConstGenericParticleHandle(Particle[1]));
		}

		for (int32 Index = 0; Index < ManifoldPoints.Num(); Index++)
		{
			FManifoldPoint& ManifoldPoint = ManifoldPoints[Index];
			GetWorldSpaceManifoldPoint(ManifoldPoint, P0, Q0, P1, Q1, ManifoldPoint.ContactPoint.Location, ManifoldPoint.ContactPoint.Phi);

			ManifoldPoint.bInsideStaticFrictionCone = bUseManifold;

			// Copy currently active point
			if (ManifoldPoint.ContactPoint.Phi < Manifold.Phi)
			{
				SetActiveContactPoint(ManifoldPoint.ContactPoint);
			}
		}
	}

	void FPBDCollisionConstraint::AddOneshotManifoldContact(const FContactPoint& ContactPoint, const FReal Dt)
	{
		int32 ManifoldPointIndex = AddManifoldPoint(ContactPoint, Dt);

		// Copy currently active point
		if (ManifoldPoints[ManifoldPointIndex].ContactPoint.Phi < Manifold.Phi)
		{
			SetActiveContactPoint(ManifoldPoints[ManifoldPointIndex].ContactPoint);
		}

		bUseIncrementalManifold = false;
	}

	void FPBDCollisionConstraint::AddIncrementalManifoldContact(const FContactPoint& ContactPoint, const FReal Dt)
	{
		if (bUseManifold)
		{
			// See if the manifold point already exists
			int32 ManifoldPointIndex = FindManifoldPoint(ContactPoint);
			if (ManifoldPointIndex >= 0)
			{
				// This contact point is already in the manifold - update the state
				UpdateManifoldPoint(ManifoldPointIndex, ContactPoint, Dt);
			}
			else
			{
				// This is a new manifold point - capture the state and generate initial properties
				ManifoldPointIndex = AddManifoldPoint(ContactPoint, Dt);
			}

			// Copy currently active point
			if (ManifoldPoints[ManifoldPointIndex].ContactPoint.Phi < Manifold.Phi)
			{
				SetActiveContactPoint(ManifoldPoints[ManifoldPointIndex].ContactPoint);
			}
		}
		else 
		{
			// We are not using manifolds - reuse the first and only point
			if (ManifoldPoints.Num() == 0)
			{
				ManifoldPoints.Add(ContactPoint);
			}
			else
			{
				ManifoldPoints[0].ContactPoint = ContactPoint;
			}

			InitManifoldPoint(0, Dt);

			SetActiveContactPoint(ManifoldPoints[0].ContactPoint);
		}

		bUseIncrementalManifold = true;
	}

	void FPBDCollisionConstraint::ClearManifold()
	{
		ManifoldPoints.Reset();
	}

	void FPBDCollisionConstraint::InitManifoldPoint(const int32 ManifoldPointIndex, FReal Dt)
	{
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];

		FConstGenericParticleHandle Particle0 = Particle[0];
		FConstGenericParticleHandle Particle1 = Particle[1];
		if (!Particle0.IsValid() || !Particle1.IsValid())
		{
			// @todo(chaos): This is just for unit tests testing one-shot manifolds - remove it somehow... 
			// maybe ConstructConvexConvexOneShotManifold should not take a Constraint
			return;
		}

		// Update the derived contact state (CoM relative data)
		UpdateManifoldPointFromContact(ManifoldPointIndex);

		// Initialize the previous contact transforms if the data is available, otherwise reset them to current
		TryRestoreFrictionData(ManifoldPointIndex);
	}

	int32 FPBDCollisionConstraint::AddManifoldPoint(const FContactPoint& ContactPoint, const FReal Dt)
	{
		int32 ManifoldPointIndex = ManifoldPoints.Add(ContactPoint);

		InitManifoldPoint(ManifoldPointIndex, Dt);

		return ManifoldPointIndex;
	}

	void FPBDCollisionConstraint::UpdateManifoldPoint(int32 ManifoldPointIndex, const FContactPoint& ContactPoint, const FReal Dt)
	{
		// We really need to know that it's exactly the same contact and not just a close one to update it here
		// otherwise the PrevLocalContactPoint1 we calculated is not longer for the correct point.
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];
		ManifoldPoint.ContactPoint = ContactPoint;
		UpdateManifoldPointFromContact(ManifoldPointIndex);
	}

	// Update the derived contact state (CoM relative data)
	void FPBDCollisionConstraint::UpdateManifoldPointFromContact(const int32 ManifoldPointIndex)
	{
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];
		FConstGenericParticleHandle Particle0 = Particle[0];
		FConstGenericParticleHandle Particle1 = Particle[1];

		const FVec3 LocalContactPoint0 = ImplicitTransform[0].TransformPositionNoScale(ManifoldPoint.ContactPoint.ShapeContactPoints[0]);	// Particle Space on body 0
		const FVec3 LocalContactPoint1 = ImplicitTransform[1].TransformPositionNoScale(ManifoldPoint.ContactPoint.ShapeContactPoints[1]);	// Particle Space on body 1

		const FVec3 CoMContactPoint0 = Particle0->RotationOfMass().Inverse() * (LocalContactPoint0 - Particle0->CenterOfMass());	// CoM Space on Body 0
		const FVec3 CoMContactPoint1 = Particle1->RotationOfMass().Inverse() * (LocalContactPoint1 - Particle1->CenterOfMass());	// CoM Space on Body 1
		ManifoldPoint.CoMContactPoints[0] = CoMContactPoint0;
		ManifoldPoint.CoMContactPoints[1] = CoMContactPoint1;

		// We now assume that the low-level collision detection functions initialize the world-space contact in the way we want, which is as below...
		//const FVec3 WorldContactPoint0 = Particle0->P() + Particle0->Q().RotateVector(LocalContactPoint0);
		//const FVec3 WorldContactPoint1 = Particle1->P() + Particle1->Q().RotateVector(LocalContactPoint1);
		//ManifoldPoint.ContactPoint.Location = FReal(0.5) * (WorldContactPoint0 + WorldContactPoint1);
		//ManifoldPoint.ContactPoint.Phi = FVec3::DotProduct(WorldContactPoint0 - WorldContactPoint1, ManifoldPoint.ContactPoint.Normal);
	}

	void FPBDCollisionConstraint::SetActiveContactPoint(const FContactPoint& ContactPoint)
	{
		// @todo(chaos): once we settle on manifolds we should just store the index
		Manifold.Location = ContactPoint.Location;
		Manifold.Normal = ContactPoint.Normal;
		Manifold.Phi = ContactPoint.Phi;
	}

	void FPBDCollisionConstraint::GetWorldSpaceContactPositions(
		const FManifoldPoint& ManifoldPoint,
		const FVec3& P0,			// World-Space CoM
		const FRotation3& Q0,		// World-Space CoM
		const FVec3& P1,			// World-Space CoM
		const FRotation3& Q1,		// World-Space CoM
		FVec3& OutWorldPosition0,
		FVec3& OutWorldPosition1)
	{
		OutWorldPosition0 = P0 + Q0.RotateVector(ManifoldPoint.CoMContactPoints[0]);
		OutWorldPosition1 = P1 + Q1.RotateVector(ManifoldPoint.CoMContactPoints[1]);
	}

	void FPBDCollisionConstraint::GetCoMContactPositionsFromWorld(
		const FManifoldPoint& ManifoldPoint,
		const FVec3& PCoM0,
		const FRotation3& QCoM0,
		const FVec3& PCoM1,
		const FRotation3& QCoM1,
		const FVec3& WorldPoint0,
		const FVec3& WorldPoint1,
		FVec3& OutCoMPoint0,
		FVec3& OutCoMPoint1)
	{
		// Invert the transformation in GetWorldSpaceContactPositions() and return CoM space contact locations.
		OutCoMPoint0 = QCoM0.UnrotateVector(WorldPoint0 - PCoM0);
		OutCoMPoint1 = QCoM1.UnrotateVector(WorldPoint1 - PCoM1);
	}

	void FPBDCollisionConstraint::GetWorldSpaceManifoldPoint(
		const FManifoldPoint& ManifoldPoint,
		const FVec3& P0,			// World-Space CoM
		const FRotation3& Q0,		// World-Space CoM
		const FVec3& P1,			// World-Space CoM
		const FRotation3& Q1,		// World-Space CoM
		FVec3& OutContactLocation,
		FReal& OutContactPhi)
	{
		FVec3 ContactPos0;
		FVec3 ContactPos1;
		FPBDCollisionConstraint::GetWorldSpaceContactPositions(ManifoldPoint, P0, Q0, P1, Q1, ContactPos0, ContactPos1);

		OutContactLocation = 0.5f * (ContactPos0 + ContactPos1);
		OutContactPhi = FVec3::DotProduct(ContactPos0 - ContactPos1, ManifoldPoint.ContactPoint.Normal);
	}

	bool FPBDCollisionConstraint::CanUseManifold(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1) const
	{
		// Do not use manifolds when a body is connected by a joint to another. Manifolds do not work when the bodies may be moved
		// and rotated by significant amounts and joints can do that.
		return bChaos_Manifold_EnabledWithJoints || ((Particle0->ParticleConstraints().Num() == 0) && (Particle1->ParticleConstraints().Num() == 0));
	}

	void FPBDCollisionConstraint::ResetManifold()
	{
		ManifoldPoints.Reset();
		PrevManifoldPoints.Reset();
		Manifold.Reset();
	}

	void FPBDCollisionConstraint::SaveManifold()
	{
		// Save off the previous data for use by static friction
		// We do this by swapping the arrays and resetting to avoid allocations
		Swap(ManifoldPoints, PrevManifoldPoints);
		ManifoldPoints.Reset();
		Manifold.Reset();

		bWasManifoldRestored = false;
	}

	void FPBDCollisionConstraint::RestoreManifold()
	{
		// We want to restore the manifold as-is and will skip the narrow phase, which means we just
		// leave the manifold in place, but we still have some cleanup to do to account for slight movement of the bodies
		Manifold.Phi = TNumericLimits<FReal>::Max();

		UpdateManifoldContacts();

		bWasManifoldRestored = true;
	}

	bool FPBDCollisionConstraint::TryRestoreFrictionData(const int32 ManifoldPointIndex)
	{
		// Find the previous manifold point that matches
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];
		
		// Assume we have no matching point from the previous tick, but assume we can retain friction from now on
		// Not supported for non-manifolds yet (hopefully we don't need to)
		ManifoldPoint.bInsideStaticFrictionCone = bUseManifold;
		ManifoldPoint.StaticFrictionMax = FReal(0);

		if (bChaos_Manifold_EnableFrictionRestore)
		{
			// ManifoldPoints and PrevManifoldPoints are usually in the same order, so this loop should normally terminate in first iteration
			int32 PrevManifoldPointIndex = ManifoldPointIndex;
			for (int32 PrevManifoldPointOffset = 0; PrevManifoldPointOffset < PrevManifoldPoints.Num(); ++PrevManifoldPointOffset, ++PrevManifoldPointIndex)
			{
				if (PrevManifoldPointIndex >= PrevManifoldPoints.Num())
				{
					PrevManifoldPointIndex = 0;
				}

				const FManifoldPoint& PrevManifoldPoint = PrevManifoldPoints[PrevManifoldPointIndex];
				if (PrevManifoldPoint.bInsideStaticFrictionCone)
				{
					const FVec3 DP0 = ManifoldPoint.CoMContactPoints[0] - PrevManifoldPoint.CoMContactPoints[0];
					const FVec3 DP1 = ManifoldPoint.CoMContactPoints[1] - PrevManifoldPoint.CoMContactPoints[1];

					// If the contact point is in the same spot on one of the bodies, assume it is the same contact
					// @todo(chaos): more robust same-point test. E.g., this won't work for very small or very large objects,
					// so at least make the tolerance size-dependent
					const FReal DistanceToleranceSq = FMath::Square(Chaos_Manifold_FrictionPositionTolerance);
					if ((DP0.SizeSquared() < DistanceToleranceSq) || (DP1.SizeSquared() < DistanceToleranceSq))
					{
						// If we have a previous point, use it to set the previous-state data required for 
						// static friction otherwise reset static friction limits
						ManifoldPoint.CoMContactPoints[0] = PrevManifoldPoint.CoMContactPoints[0];
						ManifoldPoint.CoMContactPoints[1] = PrevManifoldPoint.CoMContactPoints[1];
						ManifoldPoint.StaticFrictionMax = PrevManifoldPoint.StaticFrictionMax;
						return true;
					}
				}
			}
		}

		return false;
	}

	ECollisionConstraintDirection FPBDCollisionConstraint::GetConstraintDirection(const FReal Dt) const
	{
		if (GetDisabled())
		{
			return NoRestingDependency;
		}
		// D\tau is the chacteristic time (as in GBF paper Sec 8.1)
		const FReal Dtau = Dt * Chaos_GBFCharacteristicTimeRatio; 

		const FVec3 Normal = GetNormal();
		const FReal Phi = GetPhi();
		if (GetPhi() >= GetCullDistance())
		{
			return NoRestingDependency;
		}

		FVec3 GravityDirection = ConcreteContainer()->GetGravityDirection();
		FReal GravitySize = ConcreteContainer()->GetGravitySize();
		// When gravity is zero, we still want to sort the constraints instead of having a random order. In this case, set gravity to default gravity.
		if (GravitySize < SMALL_NUMBER)
		{
			GravityDirection = FVec3(0, 0, -1);
			GravitySize = 980.f;
		}

		// How far an object travels in gravity direction within time Dtau starting with zero velocity (as in GBF paper Sec 8.1). 
		// Theoretically this should be 0.5 * GravityMagnitude * Dtau * Dtau.
		// Omitting 0.5 to be more consistent with our integration scheme.
		// Multiplying 0.5 can alternatively be achieved by setting Chaos_GBFCharacteristicTimeRatio=sqrt(0.5)
		const FReal StepSize = GravitySize * Dtau * Dtau; 
		const FReal NormalDotG = FVec3::DotProduct(Normal, GravityDirection);
		const FReal NormalDirectionThreshold = 0.1f; // Hack
		if (NormalDotG < -NormalDirectionThreshold) // Object 0 rests on object 1
		{
			if (Phi + NormalDotG * StepSize < 0) // Hack to simulate object 0 falling (as in GBF paper Sec 8.1)
			{
				return Particle1ToParticle0;
			}
			else
			{
				return NoRestingDependency;
			}
		}
		else if (NormalDotG > NormalDirectionThreshold) // Object 1 rests on object 0
		{
			if (Phi - NormalDotG * StepSize < 0) // Hack to simulate object 1 falling (as in GBF paper Sec 8.1)
			{
				return Particle0ToParticle1;
			}
			else
			{
				return NoRestingDependency;
			}
		}
		else // Horizontal contact
		{
			return NoRestingDependency;
		}
	}
}