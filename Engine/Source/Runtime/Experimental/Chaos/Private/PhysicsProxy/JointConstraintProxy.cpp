// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/JointConstraintProxy.h"

#include "ChaosStats.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/Collision/CollisionConstraintFlags.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/Serializable.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsSolver.h"
#include "Chaos/PullPhysicsDataImp.h"

namespace Chaos
{

FJointConstraintPhysicsProxy::FJointConstraintPhysicsProxy(FJointConstraint* InConstraint, FPBDJointConstraintHandle* InHandle, UObject* InOwner)
	: Base(EPhysicsProxyType::JointConstraintType, InOwner)
	, Constraint(InConstraint) // This proxy assumes ownership of the Constraint, and will free it during DestroyOnPhysicsThread
	, Handle(InHandle)
{
	check(Constraint!=nullptr);
	Constraint->SetProxy(this);
	JointSettingsBuffer = Constraint->GetJointSettings();
}

FGeometryParticleHandle*
FJointConstraintPhysicsProxy::GetParticleHandleFromProxy(IPhysicsProxyBase* ProxyBase)
{
	if (ProxyBase)
	{
		if (ProxyBase->GetType() == EPhysicsProxyType::SingleParticleProxy)
		{
			return ((FSingleParticlePhysicsProxy*)ProxyBase)->GetHandle_LowLevel();
		}
	}
	return nullptr;
}

/**/
void FJointConstraintPhysicsProxy::BufferPhysicsResults(FDirtyJointConstraintData& Buffer)
{
	Buffer.SetProxy(*this);
	if (Constraint != nullptr && Constraint->IsValid() )
	{
		if (Handle != nullptr && (Handle->IsValid() || Handle->IsConstraintBreaking() || Handle->IsDriveTargetChanged()))
		{
			Buffer.OutputData.bIsBreaking = Handle->IsConstraintBreaking();
			Buffer.OutputData.bIsBroken = !Handle->IsConstraintEnabled();
			Buffer.OutputData.bDriveTargetChanged = Handle->IsDriveTargetChanged();
			Buffer.OutputData.Force = Handle->GetLinearImpulse();
			Buffer.OutputData.Torque = Handle->GetAngularImpulse();

			Handle->ClearConstraintBreaking(); // it's a single frame event, so reset
			Handle->ClearDriveTargetChanged(); // it's a single frame event, so reset
		}
	}
}

/**/
bool FJointConstraintPhysicsProxy::PullFromPhysicsState(const FDirtyJointConstraintData& Buffer, const int32 SolverSyncTimestamp)
{
	if (Constraint != nullptr && Constraint->IsValid())
	{
		if (Handle != nullptr && (Handle->IsValid() || Buffer.OutputData.bIsBreaking || Buffer.OutputData.bDriveTargetChanged))
		{
			Constraint->GetOutputData().bIsBreaking = Buffer.OutputData.bIsBreaking;
			Constraint->GetOutputData().bIsBroken = Buffer.OutputData.bIsBroken;
			Constraint->GetOutputData().bDriveTargetChanged = Buffer.OutputData.bDriveTargetChanged;
			Constraint->GetOutputData().Force = Buffer.OutputData.Force;
			Constraint->GetOutputData().Torque = Buffer.OutputData.Torque;
		}
	}

	return true;
}

void FJointConstraintPhysicsProxy::InitializeOnPhysicsThread(FPBDRigidsSolver* InSolver)
{
	auto& Handles = InSolver->GetParticles().GetParticleHandles();
	if (Handles.Size() && IsValid())
	{
		auto& JointConstraints = InSolver->GetJointConstraints();
		if (Constraint != nullptr)
		{
			FConstraintBase::FProxyBasePair& BasePairs = Constraint->GetParticleProxies();

			FGeometryParticleHandle* Handle0 = GetParticleHandleFromProxy(BasePairs[0]);
			FGeometryParticleHandle* Handle1 = GetParticleHandleFromProxy(BasePairs[1]);
			if (Handle0 && Handle1)
			{
				Handle = JointConstraints.AddConstraint({ Handle0,Handle1 }, Constraint->GetJointTransforms());
				Handle->SetSettings(JointSettingsBuffer);

				Handle0->AddConstraintHandle(Handle);
				Handle1->AddConstraintHandle(Handle);
			}

		}
	}
}

void FJointConstraintPhysicsProxy::DestroyOnPhysicsThread(FPBDRigidsSolver* InSolver)
{
	if (Handle && Handle->IsValid())
	{
		auto& JointConstraints = InSolver->GetJointConstraints();
		JointConstraints.RemoveConstraint(Handle->GetConstraintIndex());

	}

	if (Constraint != nullptr)
	{
		delete Constraint;
		Constraint = nullptr;
	}
}


void FJointConstraintPhysicsProxy::PushStateOnGameThread(FPBDRigidsSolver* InSolver)
{
	if (Constraint && Constraint->IsValid())
	{
		if (Constraint->IsDirty())
		{
			JointSettingsBuffer = Constraint->GetJointSettings();
			DirtyFlagsBuffer = Constraint->GetDirtyFlags();
			Constraint->ClearDirtyFlags();
		}
	}
}


void FJointConstraintPhysicsProxy::PushStateOnPhysicsThread(FPBDRigidsSolver* InSolver)
{
	if (Handle && Handle->IsValid())
	{
		if (DirtyFlagsBuffer.IsDirty())
		{
			const FPBDJointSettings& CurrentConstraintSettings = Handle->GetSettings();

			if (CurrentConstraintSettings.bCollisionEnabled != JointSettingsBuffer.bCollisionEnabled)
			{
				FConstraintBase::FProxyBasePair& BasePairs = Constraint->GetParticleProxies();
				FGeometryParticleHandle* Handle0 = GetParticleHandleFromProxy(BasePairs[0]);
				FGeometryParticleHandle* Handle1 = GetParticleHandleFromProxy(BasePairs[1]);

				// Three pieces of state to update on the physics thread. 
				// .. Mask on the particle array
				// .. Constraint collisions enabled array
				// .. IgnoreCollisionsManager
				if (Handle0 && Handle1)
				{
					FPBDRigidParticleHandle* RigidHandle0 = Handle0->CastToRigidParticle();
					FPBDRigidParticleHandle* RigidHandle1 = Handle1->CastToRigidParticle();

					// As long as one particle is a rigid we can add the ignore entry, one particle can be a static
					if (RigidHandle0 || RigidHandle1)
					{
						const FUniqueIdx ID0 = Handle0->UniqueIdx();
						const FUniqueIdx ID1 = Handle1->UniqueIdx();
						FIgnoreCollisionManager& IgnoreCollisionManager = InSolver->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();

						// For rigid/dynamic particles, add the broadphase flag and the IDs to check for disabled collisions
						if(RigidHandle0)
						{
							if(JointSettingsBuffer.bCollisionEnabled)
							{
								//can't remove collision constraint flag because we may still be ignoring collision for others
								IgnoreCollisionManager.RemoveIgnoreCollisionsFor(ID0, ID1);
							}
							else
							{
								RigidHandle0->AddCollisionConstraintFlag(ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
								IgnoreCollisionManager.AddIgnoreCollisionsFor(ID0, ID1);
							}
						}

						if(RigidHandle1)
						{
							if (JointSettingsBuffer.bCollisionEnabled)
							{
								//can't remove collision constraint flag because we may still be ignoring collision for others
								IgnoreCollisionManager.RemoveIgnoreCollisionsFor(ID1, ID0);
							}
							else
							{
								RigidHandle1->AddCollisionConstraintFlag(ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
								IgnoreCollisionManager.AddIgnoreCollisionsFor(ID1, ID0);
							}
						}
					}
				}
			}

			Handle->SetSettings(JointSettingsBuffer);
			DirtyFlagsBuffer.Clear();
		}
	}
}

}