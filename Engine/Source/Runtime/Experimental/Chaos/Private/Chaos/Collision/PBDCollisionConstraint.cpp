// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Particle/ParticleUtilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	float ChaosManifoldMatchPositionTolerance = 0.2f;		// % of object size position tolerance
	float ChaosManifoldMatchNormalTolerance = 0.02f;		// Dot product tolerance
	FAutoConsoleVariableRef CVarChaosManifoldMatchPositionTolerance(TEXT("p.Chaos.Collision.ManifoldMatchPositionTolerance"), ChaosManifoldMatchPositionTolerance, TEXT("A tolerance as a fraction of object size used to determine if two contact points are the same"));
	FAutoConsoleVariableRef CVarChaosManifoldMatchNormalTolerance(TEXT("p.Chaos.Collision.ManifoldMatchNormalTolerance"), ChaosManifoldMatchNormalTolerance, TEXT("A tolerance on the normal dot product used to determine if two contact points are the same"));

	FString FCollisionConstraintBase::ToString() const
	{
		return FString::Printf(TEXT("Particle:%s, Levelset:%s, AccumulatedImpulse:%s"), *Particle[0]->ToString(), *Particle[1]->ToString(), *AccumulatedImpulse.ToString());
	}

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
	bool FRigidBodyPointContactConstraint::AreMatchingContactPoints(const FContactPoint& A, const FContactPoint& B) const
	{
		// @todo(chaos): cache tolerances?
		const FReal Size0 = Particle[0]->Geometry()->BoundingBox().Extents().Max();
		const FReal Size1 = Particle[1]->Geometry()->BoundingBox().Extents().Max();
		const FReal DistanceTolerance = FMath::Min(Size0, Size1) * ChaosManifoldMatchPositionTolerance;
		const FReal NormalTolerance = ChaosManifoldMatchNormalTolerance;

		// If normal has changed a lot, it is a different contact
		// (This was only here to detect bad normals - it is not right for edge-edge contact tracking, but we don't do a good job of that yet anyway!)
		FReal NormalDot = FVec3::DotProduct(A.Normal, B.Normal);
		if (NormalDot < 1.0f - NormalTolerance)
		{
			return false;
		}

		// If either point in local space is the same, it is the same contact
		const float DistanceTolerance2 = DistanceTolerance * DistanceTolerance;
		for (int32 BodyIndex = 0; BodyIndex < 2; ++BodyIndex)
		{
			FVec3 DR = A.ShapeContactPoints[BodyIndex] - B.ShapeContactPoints[BodyIndex];
			FReal DRLen2 = DR.SizeSquared();
			if (DRLen2 < DistanceTolerance2)
			{
				return true;
			}
		}

		return false;
	}

	int32 FRigidBodyPointContactConstraint::FindManifoldPoint(const FContactPoint& ContactPoint) const
	{
		const int32 NumManifoldPoints = ManifoldPoints.Num();
		for (int32 ManifoldPointIndex = 0; ManifoldPointIndex < NumManifoldPoints; ++ManifoldPointIndex)
		{
			if (AreMatchingContactPoints(ContactPoint, ManifoldPoints[ManifoldPointIndex].ContactPoint))
			{
				return ManifoldPointIndex;
			}
		}
		return INDEX_NONE;
	}

	void FRigidBodyPointContactConstraint::UpdateManifold(const FContactPoint& ContactPoint)
	{
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

		// Copy point currently active point
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
		const FVec3 PrevWorldContactLocation0 = Particle0->X() + Particle0->R() * LocalContactPoint0;
		const FVec3 PrevWorldContactLocation1 = Particle1->X() + Particle1->R() * LocalContactPoint1;
		const FVec3 PrevWorldContactNormal = Particle1->R() * LocalContactNormal;
		const FReal PrevPhi = FVec3::DotProduct(PrevWorldContactLocation0 - PrevWorldContactLocation1, PrevWorldContactNormal);
		const FVec3 LocalPrevContactPoint1 = Particle1->R().Inverse() * (PrevWorldContactLocation0 - PrevPhi * PrevWorldContactNormal - Particle1->X());	// Particle Space

		ManifoldPoint.CoMContactPoints[0] = Particle0->RotationOfMass().Inverse() * (LocalContactPoint0 - Particle0->CenterOfMass());
		ManifoldPoint.CoMContactPoints[1] = Particle1->RotationOfMass().Inverse() * (LocalContactPoint1 - Particle1->CenterOfMass());
		ManifoldPoint.CoMContactNormal = Particle1->RotationOfMass().Inverse() * LocalContactNormal;
		ManifoldPoint.PrevCoMContactPoint1 = Particle1->RotationOfMass().Inverse() * (LocalPrevContactPoint1 - Particle1->CenterOfMass());
		ManifoldPoint.InitialPhi = ManifoldPoint.ContactPoint.Phi;
		ManifoldPoint.PrevPhi = PrevPhi;
	}

	int32 FRigidBodyPointContactConstraint::AddManifoldPoint(const FContactPoint& ContactPoint)
	{
		// @todo(chaos): remove the least useful manifold point when we hit some point limit...
		int32 ManifoldPointIndex = ManifoldPoints.Add(ContactPoint);

		InitManifoldPoint(ManifoldPoints[ManifoldPointIndex]);

		return ManifoldPointIndex;
	}

	void FRigidBodyPointContactConstraint::UpdateManifoldPoint(int32 ManifoldPointIndex, const FContactPoint& ContactPoint)
	{
		// We really need to know that it's exactly the same contact and not just a close one to update it here
		// otherwise the PrevLocalContactPoint1 we calculated is not longer for the correct point.
		// @todo(chaos): this will give us an incorrect InitialPhi until we have a proper one-shot manifold
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];
		ManifoldPoint.ContactPoint = ContactPoint;
		InitManifoldPoint(ManifoldPoint);
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