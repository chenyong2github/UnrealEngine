// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDJointConstraintData.h"
#include "PBDRigidsSolver.h"

namespace Chaos
{
	FJointConstraint::FJointConstraint()
		: FConstraintBase(EConstraintType::JointConstraintType)
		, JointTransforms({ FTransform::Identity, FTransform::Identity })
		, UserData(nullptr)
		, KinematicEndPoint(nullptr)
	{
	}

	FJointConstraint::FTransformPair FJointConstraint::GetJointTransforms() { return JointTransforms; }

	void FJointConstraint::SetKinematicEndPoint(FSingleParticlePhysicsProxy* InDummyParticle, FPBDRigidsSolver* Solver)
	{
		ensure(KinematicEndPoint == nullptr);
		KinematicEndPoint = InDummyParticle;
		Solver->RegisterObject(KinematicEndPoint);
	}

	const FJointConstraint::FTransformPair FJointConstraint::GetJointTransforms() const { return JointTransforms; }
	void FJointConstraint::SetJointTransforms(const Chaos::FJointConstraint::FTransformPair& InJointTransforms)
	{
		JointTransforms[0] = InJointTransforms[0];
		JointTransforms[1] = InJointTransforms[1];
	}

	void FJointConstraint::SetLinearPositionDriveEnabled(TVector<bool,3> Enabled)
	{
		SetLinearPositionDriveXEnabled(Enabled.X);
		SetLinearPositionDriveYEnabled(Enabled.Y);
		SetLinearPositionDriveZEnabled(Enabled.Z);
	}


	void FJointConstraint::SetLinearVelocityDriveEnabled(TVector<bool,3> Enabled)
	{
		SetLinearVelocityDriveXEnabled(Enabled.X);
		SetLinearVelocityDriveYEnabled(Enabled.Y);
		SetLinearVelocityDriveZEnabled(Enabled.Z);
	}

	void FJointConstraint::ReleaseKinematicEndPoint(FPBDRigidsSolver* Solver)
	{
		if (KinematicEndPoint)
		{
			Solver->UnregisterObject(KinematicEndPoint);
			KinematicEndPoint = nullptr;
		}
	}


} // Chaos
