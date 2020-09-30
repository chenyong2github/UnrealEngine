// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandle.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	float ChaosManifoldMatchPositionTolerance = 0.2f;		// % of object size position tolerance
	float ChaosManifoldMatchNormalTolerance = 0.02f;		// Dot product tolerance
	FAutoConsoleVariableRef CVarChaosManifoldMatchPositionTolerance(TEXT("p.Chaos.Collision.ManifoldMatchPositionTolerance"), ChaosManifoldMatchPositionTolerance, TEXT("A tolerance as a fraction of object size used to determine if two contact points are the same"));
	FAutoConsoleVariableRef CVarChaosManifoldMatchNormalTolerance(TEXT("p.Chaos.Collision.ManifoldMatchNormalTolerance"), ChaosManifoldMatchNormalTolerance, TEXT("A tolerance on the normal dot product used to determine if two contact points are the same"));

	bool bChaosUseIncrementalManifold = false;
	FAutoConsoleVariableRef CVarChaisUseIncrementalManifold(TEXT("p.Chaos.Collision.UseIncrementalManifold"), bChaosUseIncrementalManifold, TEXT(""));

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
	//		The contact is Vertex - Plane and there was a prior iteration collision on the same Vertex
	//		The contact is Edge - Edge and a prior iteration collision contained both edges
	//
	// But we don’t have feature IDs. So in the meantime contact points will be considered the "same" if
	//		Vertex - Plane - the local space contact position on either body is within some tolerance
	//		Edge - Edge - ?? hard...
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
			FVec3 DR = A.LocalContactPoints[BodyIndex] - B.LocalContactPoints[BodyIndex];
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
		if (bChaosUseIncrementalManifold)
		{
			int32 ManifoldPointIndex = FindManifoldPoint(ContactPoint);
			if (ManifoldPointIndex >= 0)
			{
				SetManifoldPoint(ManifoldPointIndex, ContactPoint);
			}
			else
			{
				ManifoldPointIndex = AddManifoldPoint(ContactPoint);
			}
		}

		// @todo(chaos): Legacy behaviour - not needed if using the manifold
		if (ContactPoint.Phi < Manifold.Phi)
		{
			SetActiveContactPoint(ContactPoint);
		}
	}

	void FRigidBodyPointContactConstraint::ClearManifold()
	{
		ManifoldPoints.Reset();
	}

	int32 FRigidBodyPointContactConstraint::AddManifoldPoint(const FContactPoint& ContactPoint)
	{
		// @todo(chaos): remove the least useful manifold point when we hit some point limit...
		return ManifoldPoints.Add(ContactPoint);
	}

	void FRigidBodyPointContactConstraint::SetManifoldPoint(int32 ManifoldPointIndex, const FContactPoint& ContactPoint)
	{
		ManifoldPoints[ManifoldPointIndex].ContactPoint = ContactPoint;
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
		FVec3 ContactPos0 = P0 + Q0.RotateVector(ManifoldPoint.ContactPoint.LocalContactPoints[0]);
		FVec3 ContactPos1 = P1 + Q1.RotateVector(ManifoldPoint.ContactPoint.LocalContactPoints[1]);
		FVec3 ContactNormal = Q1.RotateVector(ManifoldPoint.ContactPoint.LocalContactNormal);
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