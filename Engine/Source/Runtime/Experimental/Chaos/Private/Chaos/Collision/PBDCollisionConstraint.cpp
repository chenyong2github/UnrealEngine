// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	FRealSingle Chaos_Manifold_MatchPositionTolerance = 0.3f;		// Fraction of object size position tolerance
	FRealSingle Chaos_Manifold_MatchNormalTolerance = 0.02f;		// Dot product tolerance
	FAutoConsoleVariableRef CVarChaos_Manifold_MatchPositionTolerance(TEXT("p.Chaos.Collision.Manifold.MatchPositionTolerance"), Chaos_Manifold_MatchPositionTolerance, TEXT("A tolerance as a fraction of object size used to determine if two contact points are the same"));
	FAutoConsoleVariableRef CVarChaos_Manifold_MatchNormalTolerance(TEXT("p.Chaos.Collision.Manifold.MatchNormalTolerance"), Chaos_Manifold_MatchNormalTolerance, TEXT("A tolerance on the normal dot product used to determine if two contact points are the same"));

	bool bChaos_Manifold_EnabledWithJoints = false;
	FAutoConsoleVariableRef CVarChaos_Manifold_EnabledWithJoints(TEXT("p.Chaos.Collision.Manifold.EnabledWithJoints"), bChaos_Manifold_EnabledWithJoints, TEXT(""));

	FString FCollisionConstraintBase::ToString() const
	{
		return FString::Printf(TEXT("Particle:%s, Levelset:%s, AccumulatedImpulse:%s"), *Particle[0]->ToString(), *Particle[1]->ToString(), *AccumulatedImpulse.ToString());
	}

	// @todo(chaos): this is not an obvious implementation of operator<. We should make it a named predicate
	bool FCollisionConstraintBase::operator<(const FCollisionConstraintBase& Other) const
	{
		//sort constraints by the smallest particle idx in them first
		//if the smallest particle idx is the same for both, use the other idx

		const FParticleID ParticleIdxs[] ={Particle[0]->ParticleID(),Particle[1]->ParticleID()};
		const FParticleID OtherParticleIdxs[] ={Other.Particle[0]->ParticleID(),Other.Particle[1]->ParticleID()};

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
	bool FRigidBodyPointContactConstraint::AreMatchingContactPoints(const FContactPoint& A, const FContactPoint& B, FReal& OutScore) const
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
					OutScore = FMath::Clamp(1.0f - DRLen2 / DistanceTolerance2, 0.0f, 1.0f);
					return true;
				}
			}
		}

		return false;
	}

	int32 FRigidBodyPointContactConstraint::FindManifoldPoint(const FContactPoint& ContactPoint) const
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

	void FRigidBodyPointContactConstraint::UpdateManifoldContacts(FReal Dt)
	{
		for (int32 Index = 0; Index < ManifoldPoints.Num() ; Index++)
		{
			const FContactPoint& ContactPoint = ManifoldPoints[Index].ContactPoint;
			UpdateManifoldPoint(Index, ContactPoint, Dt);

			// Copy currently active point
			if (ContactPoint.Phi < Manifold.Phi)
			{
				SetActiveContactPoint(ContactPoint);
			}
		}
	}

	void FRigidBodyPointContactConstraint::AddOneshotManifoldContact(const FContactPoint& ContactPoint, const FReal Dt)
	{
		AddManifoldPoint(ContactPoint, Dt);

		// Copy currently active point
		if (ContactPoint.Phi < Manifold.Phi)
		{
			SetActiveContactPoint(ContactPoint);
		}
	}

	void FRigidBodyPointContactConstraint::AddIncrementalManifoldContact(const FContactPoint& ContactPoint, const FReal Dt)
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
				AddManifoldPoint(ContactPoint, Dt);
			}

			// Copy currently active point
			if (ContactPoint.Phi < Manifold.Phi)
			{
				SetActiveContactPoint(ContactPoint);
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
				ManifoldPoints[0] = ContactPoint;
			}

			InitManifoldPoint(ManifoldPoints[0], Dt);

			SetActiveContactPoint(ContactPoint);
		}
	}

	void FRigidBodyPointContactConstraint::ClearManifold()
	{
		ManifoldPoints.Reset();
	}

	void FRigidBodyPointContactConstraint::InitManifoldPoint(FManifoldPoint& ManifoldPoint, FReal Dt)
	{
		// @todo(chaos): This is only needed for unit tests - make it more easily unit testable
		if ((Particle[0] == nullptr) || (Particle[1] == nullptr))
		{
			return;
		}

		FConstGenericParticleHandle Particle0 = Particle[0];
		FConstGenericParticleHandle Particle1 = Particle[1];

		// @todo(chaos): determine potentially resting contact case based on contact velocity
		// @todo(chaos): support static friction position correction for non-manifold contacts (spheres, point clouds, etc)
		ManifoldPoint.bPotentialRestingContact = bUseManifold;

		// Update the derived contact state (CoM relative data)
		UpdateManifoldPointFromContact(ManifoldPoint);

		// World-space contact state used below
		// @todo(chaos): some of this should already be known - optimize
		const FRigidTransform3 WorldCoMTransform0 = FParticleUtilities::GetCoMWorldTransform(Particle0);
		const FRigidTransform3 WorldCoMTransform1 = FParticleUtilities::GetCoMWorldTransform(Particle1);
		FVec3 WorldContactLocation, WorldContactNormal;
		FReal WorldContactPhi;
		GetWorldSpaceManifoldPoint(ManifoldPoint, WorldCoMTransform0.GetLocation(), WorldCoMTransform0.GetRotation(), WorldCoMTransform1.GetLocation(), WorldCoMTransform1.GetRotation(), WorldContactLocation, WorldContactNormal, WorldContactPhi);

		// Calculate and store the data required for static friction and restitution in PushOut
		// We use PreV and PreW to support incremental manifold generation. In this case, manifold points
		// can be added after we have already run some solver iterations, which gives us an incorrect initial
		// velocity if we just use V and W (one-shots will work the same either way since V=PreV on first pass)
		const FVec3 WorldContactVel0 = Particle0->PreV() + FVec3::CrossProduct(Particle0->PreW(), WorldCoMTransform0.GetRotation() * ManifoldPoint.CoMContactPoints[0]);
		const FVec3 WorldContactVel1 = Particle1->PreV() + FVec3::CrossProduct(Particle1->PreW(), WorldCoMTransform1.GetRotation() * ManifoldPoint.CoMContactPoints[1]);
		const FReal WorldContactVelNorm = FVec3::DotProduct(WorldContactVel0 - WorldContactVel1, WorldContactNormal);
		ManifoldPoint.InitialContactVelocity = WorldContactVelNorm;

		// Store the initial penetration depth for use with restitution with PBD
		// NOTE: This is incorrect if we are updating the point each iteration (which we are for incremental manifolds but not one-shots - see UpdateManifoldPoint)
		ManifoldPoint.InitialPhi = WorldContactPhi;
	}

	void FRigidBodyPointContactConstraint::CalculatePrevCoMContactPoints(
		const FConstGenericParticleHandle Particle0,
		const FConstGenericParticleHandle Particle1,
		FManifoldPoint& ManifoldPoint,
		FReal Dt,
		FVec3& OutPrevCoMContactPoint0,
		FVec3& OutPrevCoMContactPoint1) const
	{
		// Recalculate the previous local-space contact position on the plane owner. This is used by static friction where
		// we try to move the contact points back to their previous relative positions.
		auto CalculatePrevCoMTransform = [Dt](const FConstGenericParticleHandle& ParticleHandle) -> FRigidTransform3
		{
			FRigidTransform3 PrevCoMTransform = FParticleUtilitiesXR::GetCoMWorldTransform(ParticleHandle);
			if (ParticleHandle->ObjectState() == EObjectStateType::Kinematic && (ParticleHandle->V().SizeSquared() > 0.0f || ParticleHandle->W().SizeSquared() > 0.0f))
			{
				// Undo velocity integration (see KinematicTargets) for kinematic bodies
				PrevCoMTransform.AddToTranslation(-Dt * ParticleHandle->V());
				PrevCoMTransform.SetRotation(FRotation3::IntegrateRotationWithAngularVelocity(PrevCoMTransform.GetRotation(), -ParticleHandle->W(), Dt));
			}
			return PrevCoMTransform; 
		};

		const FRigidTransform3 PrevCoMTransform0 = CalculatePrevCoMTransform(Particle0);
		const FRigidTransform3 PrevCoMTransform1 = CalculatePrevCoMTransform(Particle1);

		const FVec3 PrevWorldContactLocation0 = PrevCoMTransform0.GetTranslation() + PrevCoMTransform0.GetRotation() * ManifoldPoint.CoMContactPoints[0];
		const FVec3 PrevWorldContactLocation1 = PrevCoMTransform1.GetTranslation() + PrevCoMTransform1.GetRotation() * ManifoldPoint.CoMContactPoints[1];
		FVec3 PrevCoMContactPoint0 = ManifoldPoint.CoMContactPoints[0];
		FVec3 PrevCoMContactPoint1 = ManifoldPoint.CoMContactPoints[1];
		if (ManifoldPoint.ContactPoint.ContactNormalOwnerIndex == 0)
		{
			const FVec3 PrevWorldContactNormal = PrevCoMTransform0.GetRotation() * ManifoldPoint.CoMContactNormal;
			const FReal PrevPhi = FVec3::DotProduct(PrevWorldContactLocation0 - PrevWorldContactLocation1, PrevWorldContactNormal);
			PrevCoMContactPoint0 = PrevCoMTransform0.GetRotation().Inverse() * (PrevWorldContactLocation1 + PrevPhi * PrevWorldContactNormal - PrevCoMTransform0.GetTranslation());
		}
		else
		{
			const FVec3 PrevWorldContactNormal = PrevCoMTransform1.GetRotation() * ManifoldPoint.CoMContactNormal;
			const FReal PrevPhi = FVec3::DotProduct(PrevWorldContactLocation0 - PrevWorldContactLocation1, PrevWorldContactNormal);
			PrevCoMContactPoint1 = PrevCoMTransform1.GetRotation().Inverse() * (PrevWorldContactLocation0 - PrevPhi * PrevWorldContactNormal - PrevCoMTransform1.GetTranslation());
		}

		OutPrevCoMContactPoint0 = PrevCoMContactPoint0;
		OutPrevCoMContactPoint1 = PrevCoMContactPoint1;
	}

	int32 FRigidBodyPointContactConstraint::AddManifoldPoint(const FContactPoint& ContactPoint, const FReal Dt)
	{
		int32 ManifoldPointIndex = ManifoldPoints.Add(ContactPoint);

		InitManifoldPoint(ManifoldPoints[ManifoldPointIndex], Dt);

		return ManifoldPointIndex;
	}

	void FRigidBodyPointContactConstraint::UpdateManifoldPoint(int32 ManifoldPointIndex, const FContactPoint& ContactPoint, const FReal Dt)
	{
		// We really need to know that it's exactly the same contact and not just a close one to update it here
		// otherwise the PrevLocalContactPoint1 we calculated is not longer for the correct point.
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];
		ManifoldPoint.ContactPoint = ContactPoint;
		UpdateManifoldPointFromContact(ManifoldPoint);
	}

	// Update the derived contact state (CoM relative data)
	// @todo(chaos): we shouldn't have to recalculate local space positions - they were available in collision update
	void FRigidBodyPointContactConstraint::UpdateManifoldPointFromContact(FManifoldPoint& ManifoldPoint)
	{
		FConstGenericParticleHandle Particle0 = Particle[0];
		FConstGenericParticleHandle Particle1 = Particle[1];

		check(ManifoldPoint.ContactPoint.ContactNormalOwnerIndex >= 0);
		check(ManifoldPoint.ContactPoint.ContactNormalOwnerIndex < 2);
		const FRigidTransform3& PlaneTransform = ImplicitTransform[ManifoldPoint.ContactPoint.ContactNormalOwnerIndex];
		const FVec3 LocalContactPoint0 = ImplicitTransform[0].TransformPositionNoScale(ManifoldPoint.ContactPoint.ShapeContactPoints[0]);	// Particle Space on body 0
		const FVec3 LocalContactPoint1 = ImplicitTransform[1].TransformPositionNoScale(ManifoldPoint.ContactPoint.ShapeContactPoints[1]);	// Particle Space on body 1
		const FVec3 LocalContactNormal = PlaneTransform.TransformNormalNoScale(ManifoldPoint.ContactPoint.ShapeContactNormal);				// Particle Space on Plane owner

		const FRotation3& PlaneRotationOfMass = (ManifoldPoint.ContactPoint.ContactNormalOwnerIndex == 0) ? Particle0->RotationOfMass() : Particle1->RotationOfMass();
		const FVec3 CoMContactPoint0 = Particle0->RotationOfMass().Inverse() * (LocalContactPoint0 - Particle0->CenterOfMass());	// CoM Space on Body 0
		const FVec3 CoMContactPoint1 = Particle1->RotationOfMass().Inverse() * (LocalContactPoint1 - Particle1->CenterOfMass());	// CoM Space on Body 1
		const FVec3 CoMContactNormal = PlaneRotationOfMass.Inverse() * LocalContactNormal;											// CoM Space on Planer owner
		ManifoldPoint.CoMContactPoints[0] = CoMContactPoint0;
		ManifoldPoint.CoMContactPoints[1] = CoMContactPoint1;
		ManifoldPoint.CoMContactNormal = CoMContactNormal;
	}

	void FRigidBodyPointContactConstraint::SetActiveContactPoint(const FContactPoint& ContactPoint)
	{
		// @todo(chaos): once we settle on manifolds we should just store the index
		Manifold.Location = ContactPoint.Location;
		Manifold.Normal = ContactPoint.Normal;
		Manifold.Phi = ContactPoint.Phi;
	}

	void FRigidBodyPointContactConstraint::GetWorldSpaceManifoldPoint(
		const FManifoldPoint& ManifoldPoint,
		const FVec3& P0,			// World-Space CoM
		const FRotation3& Q0,		// World-Space CoM
		const FVec3& P1,			// World-Space CoM
		const FRotation3& Q1,		// World-Space CoM
		FVec3& OutContactLocation,
		FVec3& OutContactNormal,
		FReal& OutContactPhi)
	{
		const FReal Margin0 = ManifoldPoint.ContactPoint.ShapeMargins[0];
		const FReal Margin1 = ManifoldPoint.ContactPoint.ShapeMargins[1];
		const FRotation3& PlaneQ = (ManifoldPoint.ContactPoint.ContactNormalOwnerIndex == 0) ? Q0 : Q1;

		const FVec3 ContactNormal = PlaneQ.RotateVector(ManifoldPoint.CoMContactNormal);
		const FVec3 ContactPos0 = P0 + Q0.RotateVector(ManifoldPoint.CoMContactPoints[0]) - Margin0 * ContactNormal;
		const FVec3 ContactPos1 = P1 + Q1.RotateVector(ManifoldPoint.CoMContactPoints[1]) + Margin1 * ContactNormal;

		OutContactLocation = 0.5f * (ContactPos0 + ContactPos1);
		OutContactNormal = ContactNormal;
		OutContactPhi = FVec3::DotProduct(ContactPos0 - ContactPos1, ContactNormal);
	}


	FManifoldPoint& FRigidBodyPointContactConstraint::SetActiveManifoldPoint(
		int32 ManifoldPointIndex,
		const FVec3& P0,
		const FRotation3& Q0,
		const FVec3& P1,
		const FRotation3& Q1)
	{
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];

		// Update the world-space state in the manifold point
		GetWorldSpaceManifoldPoint(ManifoldPoint, P0, Q0, P1, Q1, ManifoldPoint.ContactPoint.Location, ManifoldPoint.ContactPoint.Normal, ManifoldPoint.ContactPoint.Phi);
	
		// Copy the world-space state into the active contact
		SetActiveContactPoint(ManifoldPoint.ContactPoint);

		return ManifoldPoint;
	}

	bool FRigidBodyPointContactConstraint::CanUseManifold(FGeometryParticleHandle* Particle0, FGeometryParticleHandle* Particle1) const
	{
		// Do not use manifolds when a body is connected by a joint to another. Manifolds do not work when the bodies may be moved
		// and rotated by significant amounts and joints can do that.
		return bChaos_Manifold_EnabledWithJoints || ((Particle0->ParticleConstraints().Num() == 0) && (Particle1->ParticleConstraints().Num() == 0));
	}
}