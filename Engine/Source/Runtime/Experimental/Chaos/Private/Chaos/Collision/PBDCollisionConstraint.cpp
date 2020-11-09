// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	float Chaos_Manifold_MatchPositionTolerance = 0.3f;		// Fraction of object size position tolerance
	float Chaos_Manifold_MatchNormalTolerance = 0.02f;		// Dot product tolerance
	FAutoConsoleVariableRef CVarChaos_Manifold_MatchPositionTolerance(TEXT("p.Chaos.Collision.Manifold.MatchPositionTolerance"), Chaos_Manifold_MatchPositionTolerance, TEXT("A tolerance as a fraction of object size used to determine if two contact points are the same"));
	FAutoConsoleVariableRef CVarChaos_Manifold_MatchNormalTolerance(TEXT("p.Chaos.Collision.Manifold.MatchNormalTolerance"), Chaos_Manifold_MatchNormalTolerance, TEXT("A tolerance on the normal dot product used to determine if two contact points are the same"));

	bool bChaos_Manifold_UpdateMatchedContact = true;
	FAutoConsoleVariableRef CVarChaos_Manifold_UpdateMatchedContact(TEXT("p.Chaos.Collision.Manifold.UpdateMatchedContact"), bChaos_Manifold_UpdateMatchedContact, TEXT(""));

	int32 Chaos_Manifold_MinArraySize = 0;
	FAutoConsoleVariableRef CVarChaos_Manifold_MinArraySize(TEXT("p.Chaos.Collision.Manifold.MinArraySize"), Chaos_Manifold_MinArraySize, TEXT(""));

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
			const float DistanceTolerance2 = DistanceTolerance * DistanceTolerance;
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

	void FRigidBodyPointContactConstraint::UpdateOneShotManifoldContacts()
	{
		for (int32 Index = 0; Index < ManifoldPoints.Num() ; Index++)
		{
			const FContactPoint& ContactPoint = ManifoldPoints[Index].ContactPoint;
			UpdateManifoldPoint(Index, ContactPoint);

			// Copy currently active point
			if (ContactPoint.Phi < Manifold.Phi)
			{
				SetActiveContactPoint(ContactPoint);
			}
		}
	}

	void FRigidBodyPointContactConstraint::AddOneshotManifoldContact(const FContactPoint& ContactPoint, bool bInInitialize)
	{
		if (bUseOneShotManifold)
		{
			AddManifoldPoint(ContactPoint, bInInitialize);
		}

		// Copy currently active point
		if (ContactPoint.Phi < Manifold.Phi)
		{
			SetActiveContactPoint(ContactPoint);
		}
	}

	void FRigidBodyPointContactConstraint::UpdateManifold(const FContactPoint& ContactPoint)
	{
		ensure(bUseOneShotManifold == false);

		if (bUseIncrementalManifold)
		{
			int32 ManifoldPointIndex = FindManifoldPoint(ContactPoint);
			if (ManifoldPointIndex >= 0)
			{
				// This contact point is already in the manifold - update the state (maybe)
				UpdateManifoldPoint(ManifoldPointIndex, ContactPoint);
			}
			else
			{
				// This is a new manifold point - capture the state and generate initial properties
				ManifoldPointIndex = AddManifoldPoint(ContactPoint);
			}
		}

		// Copy currently active point
		if (ContactPoint.Phi < Manifold.Phi)
		{
			SetActiveContactPoint(ContactPoint);
		}
	}

	void FRigidBodyPointContactConstraint::ClearManifold()
	{
		ManifoldPoints.Reset();
	}

	void FRigidBodyPointContactConstraint::InitManifoldPoint(FManifoldPoint& ManifoldPoint)
	{
		TConstGenericParticleHandle<FReal, 3> Particle0 = Particle[0];
		TConstGenericParticleHandle<FReal, 3> Particle1 = Particle[1];

		// @todo(chaos): determine potentially resting contact case based on contact velocity
		ManifoldPoint.bPotentialRestingContact = true;

		// Calculate and store the data required for static friction and restitution in PushOut
		//	- contact point on second shape at previous location (to enforce static friction)
		//	- initial and previous separations (for restitution)
		// @todo(chaos): we shouldn't have to recalculate this...it was available in collision update
		const FVec3 LocalContactPoint0 = ImplicitTransform[0].TransformPosition(ManifoldPoint.ContactPoint.ShapeContactPoints[0]);	// Particle Space
		const FVec3 LocalContactPoint1 = ImplicitTransform[1].TransformPosition(ManifoldPoint.ContactPoint.ShapeContactPoints[1]);	// Particle Space
		const FVec3 LocalContactNormal = ImplicitTransform[1].TransformVector(ManifoldPoint.ContactPoint.ShapeContactNormal);		// Particle Space

		const FVec3 CoMContactPoint0 = Particle0->RotationOfMass().Inverse() * (LocalContactPoint0 - Particle0->CenterOfMass());
		const FVec3 CoMContactPoint1 = Particle1->RotationOfMass().Inverse() * (LocalContactPoint1 - Particle1->CenterOfMass());
		const FVec3 CoMContactNormal = Particle1->RotationOfMass().Inverse() * LocalContactNormal;
		ManifoldPoint.CoMContactPoints[0] = CoMContactPoint0;
		ManifoldPoint.CoMContactPoints[1] = CoMContactPoint1;
		ManifoldPoint.CoMContactNormal = CoMContactNormal;

		const FVec3 PrevWorldContactLocation0 = Particle0->X() + Particle0->R() * LocalContactPoint0;
		const FVec3 PrevWorldContactLocation1 = Particle1->X() + Particle1->R() * LocalContactPoint1;
		const FVec3 PrevWorldContactNormal = Particle1->R() * LocalContactNormal;
		const FReal PrevPhi = FVec3::DotProduct(PrevWorldContactLocation0 - PrevWorldContactLocation1, PrevWorldContactNormal);
		const FVec3 PrevLocalContactPoint1 = Particle1->R().Inverse() * (PrevWorldContactLocation0 - PrevPhi * PrevWorldContactNormal - Particle1->X());	// Particle Space
		const FVec3 PrevCoMContactPoint1 = Particle1->RotationOfMass().Inverse()* (PrevLocalContactPoint1 - Particle1->CenterOfMass());
		const FVec3 PrevWorldContactVel0 = Particle0->PreV() + FVec3::CrossProduct(Particle0->PreW(), PrevWorldContactLocation0 - Particle0->X());
		const FVec3 PrevWorldContactVel1 = Particle1->PreV() + FVec3::CrossProduct(Particle1->PreW(), PrevWorldContactLocation1 - Particle1->X());
		const FReal PrevWorldContactVelNorm = FVec3::DotProduct(PrevWorldContactVel0 - PrevWorldContactVel1, PrevWorldContactNormal);
		ManifoldPoint.PrevCoMContactPoint1 = PrevCoMContactPoint1;
		ManifoldPoint.InitialContactVelocity = PrevWorldContactVelNorm;

		// NOTE: This is incorrect of we are updating the point each iteration (which we are - see UpdateManifoldPoint)
		ManifoldPoint.InitialPhi = ManifoldPoint.ContactPoint.Phi;
	}

	int32 FRigidBodyPointContactConstraint::AddManifoldPoint(const FContactPoint& ContactPoint, bool bInInitialize)
	{
		// @todo(chaos): remove the least useful manifold point when we hit some point limit...
		if (ManifoldPoints.Max() < Chaos_Manifold_MinArraySize)
		{
			ManifoldPoints.Reserve(Chaos_Manifold_MinArraySize);
		}
		int32 ManifoldPointIndex = ManifoldPoints.Add(ContactPoint);

		if (bInInitialize)
		{
			InitManifoldPoint(ManifoldPoints[ManifoldPointIndex]);
		}

		return ManifoldPointIndex;
	}

	void FRigidBodyPointContactConstraint::UpdateManifoldPoint(int32 ManifoldPointIndex, const FContactPoint& ContactPoint)
	{
		// We really need to know that it's exactly the same contact and not just a close one to update it here
		// otherwise the PrevLocalContactPoint1 we calculated is not longer for the correct point.
		// @todo(chaos): this will give us an incorrect InitialPhi until we have a proper one-shot manifold
		if (bChaos_Manifold_UpdateMatchedContact)
		{
			FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];
			ManifoldPoint.ContactPoint = ContactPoint;
			InitManifoldPoint(ManifoldPoint);
		}
	}

	void FRigidBodyPointContactConstraint::SetActiveContactPoint(const FContactPoint& ContactPoint)
	{
		// @todo(chaos): once we settle on manifolds we should just store the index
		Manifold.Location = ContactPoint.Location;
		Manifold.Normal = ContactPoint.Normal;
		Manifold.Phi = ContactPoint.Phi;
	}

	FManifoldPoint& FRigidBodyPointContactConstraint::SetActiveManifoldPoint(
		int32 ManifoldPointIndex,
		const FVec3& P0,
		const FRotation3& Q0,
		const FVec3& P1,
		const FRotation3& Q1)
	{
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];

		// Update the world-space point state based on current particle transforms
		const FVec3 ContactPos0 = P0 + Q0.RotateVector(ManifoldPoint.CoMContactPoints[0]);
		const FVec3 ContactPos1 = P1 + Q1.RotateVector(ManifoldPoint.CoMContactPoints[1]);
		const FVec3 ContactNormal = Q1.RotateVector(ManifoldPoint.CoMContactNormal);
		ManifoldPoint.ContactPoint.Location = 0.5f * (ContactPos0 + ContactPos1);
		ManifoldPoint.ContactPoint.Normal = ContactNormal;
		ManifoldPoint.ContactPoint.Phi = FVec3::DotProduct(ContactPos0 - ContactPos1, ContactNormal);
	
		SetActiveContactPoint(ManifoldPoint.ContactPoint);

		return ManifoldPoint;
	}


	void FRigidBodyMultiPointContactConstraint::InitManifoldTolerance(const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1, const FReal InPositionTolerance, const FReal InRotationTolerance)
	{
		InitialPositionSeparation = ParticleTransform1.GetTranslation() - ParticleTransform0.GetTranslation();
		InitialRotationSeparation = FRotation3::CalculateAngularDelta(ParticleTransform0.GetRotation(), ParticleTransform1.GetRotation());
		PositionToleranceSq = InPositionTolerance * InPositionTolerance;
		RotationToleranceSq = InRotationTolerance * InRotationTolerance;
		bUseManifoldTolerance = true;
	}

	bool FRigidBodyMultiPointContactConstraint::IsManifoldWithinToleranceImpl(const FRigidTransform3& ParticleTransform0, const FRigidTransform3& ParticleTransform1)
	{
		const FVec3 PositionSeparation = ParticleTransform1.GetTranslation() - ParticleTransform0.GetTranslation();
		const FVec3 PositionDelta = PositionSeparation - InitialPositionSeparation;
		if (PositionDelta.SizeSquared() > PositionToleranceSq)
		{
			return false;
		}

		const FVec3 RotationSeparation = FRotation3::CalculateAngularDelta(ParticleTransform0.GetRotation(), ParticleTransform1.GetRotation());
		const FVec3 RotationDelta = RotationSeparation - InitialRotationSeparation;
		if (RotationDelta.SizeSquared() > RotationToleranceSq)
		{
			return false;
		}

		return true;
	}
}