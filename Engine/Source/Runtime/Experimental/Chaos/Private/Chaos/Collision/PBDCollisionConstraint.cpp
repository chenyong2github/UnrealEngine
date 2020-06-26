// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/ParticleHandle.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
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
		} else if(ParticleIdxs[MinIdx] == OtherParticleIdxs[OtherMinIdx])
		{
			return ParticleIdxs[!MinIdx] < OtherParticleIdxs[!OtherMinIdx];
		}

		return false;
	}

	void FCollisionConstraintsArray::SortConstraints()
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_SortConstraints);
		SinglePointConstraints.Sort();
		SinglePointSweptConstraints.Sort();
		MultiPointConstraints.Sort();
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