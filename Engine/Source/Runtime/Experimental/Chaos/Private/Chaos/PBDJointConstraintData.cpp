// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDJointConstraintData.h"

namespace Chaos
{

	FJointConstraint::FJointConstraint()
		: Proxy(nullptr)
		, JointParticles({ nullptr,nullptr })
		, JointTransforms({ FTransform::Identity, FTransform::Identity })
	{
	}



	void FJointConstraint::SetProxy(IPhysicsProxyBase* InProxy)
	{
		Proxy = InProxy;
		if (Proxy)
		{
			if (FPhysicsSolverBase* PhysicsSolverBase = Proxy->GetSolver<FPhysicsSolverBase>())
			{
				PhysicsSolverBase->AddDirtyProxy(Proxy);
			}
		}
	}

	bool FJointConstraint::IsValid() const
	{
		return Proxy != nullptr;
	}

	FJointConstraint::FParticlePair FJointConstraint::GetJointParticles() { return JointParticles; }
	const FJointConstraint::FParticlePair FJointConstraint::GetJointParticles() const { return JointParticles; }
	void FJointConstraint::SetJointParticles(const Chaos::FJointConstraint::FParticlePair& InJointParticles)
	{
		JointParticles[0] = InJointParticles[0];
		JointParticles[1] = InJointParticles[1];
	}


	FJointConstraint::FTransformPair FJointConstraint::GetJointTransforms() { return JointTransforms; }
	const FJointConstraint::FTransformPair FJointConstraint::GetJointTransforms() const { return JointTransforms; }
	void FJointConstraint::SetJointTransforms(const Chaos::FJointConstraint::FTransformPair& InJointParticles)
	{
		JointTransforms[0] = InJointParticles[0];
		JointTransforms[1] = InJointParticles[1];
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



} // Chaos
