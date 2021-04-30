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


FJointConstraintPhysicsProxy::FJointConstraintPhysicsProxy(Chaos::FJointConstraint* InConstraint, FConstraintHandle* InHandle, UObject* InOwner)
	: Base(InOwner)
	, Constraint(InConstraint) // This proxy assumes ownership of the Constraint, and will free it during DestroyOnPhysicsThread
	, Handle(InHandle)
	, bInitialized(false)
{
	check(Constraint!=nullptr);
	Constraint->SetProxy(this);
	JointSettingsBuffer = Constraint->GetJointSettings();

}

Chaos::TGeometryParticleHandle<Chaos::FReal, 3>*
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
void FJointConstraintPhysicsProxy::BufferPhysicsResults(Chaos::FDirtyJointConstraintData& Buffer)
{
	Buffer.SetProxy(*this);
	if (Constraint != nullptr && Constraint->IsValid() )
	{
		if (Handle != nullptr && Handle->IsValid())
		{
			Buffer.OutputData.bIsBroken = !Handle->IsConstraintEnabled();
			Buffer.OutputData.Force = Handle->GetLinearImpulse();
			Buffer.OutputData.Torque = Handle->GetAngularImpulse();
		}
	}
}

/**/
bool FJointConstraintPhysicsProxy::PullFromPhysicsState(const Chaos::FDirtyJointConstraintData& Buffer, const int32 SolverSyncTimestamp)
{
	if (Constraint != nullptr && Constraint->IsValid())
	{
		if (Handle != nullptr && Handle->IsValid())
		{
			Constraint->GetOutputData().bIsBroken = Buffer.OutputData.bIsBroken;
			Constraint->GetOutputData().Force = Buffer.OutputData.Force;
			Constraint->GetOutputData().Torque = Buffer.OutputData.Torque;
		}
	}

	return true;
}

void FJointConstraintPhysicsProxy::InitializeOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver)
{
	auto& Handles = InSolver->GetParticles().GetParticleHandles();
	if (Handles.Size() && IsValid())
	{
		auto& JointConstraints = InSolver->GetJointConstraints();
		if (Constraint != nullptr)
		{
			Chaos::FConstraintBase::FProxyBasePair& BasePairs = Constraint->GetParticleProxies();

			Chaos::TGeometryParticleHandle<Chaos::FReal, 3>* Handle0 = GetParticleHandleFromProxy(BasePairs[0]);
			Chaos::TGeometryParticleHandle<Chaos::FReal, 3>* Handle1 = GetParticleHandleFromProxy(BasePairs[1]);
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

void FJointConstraintPhysicsProxy::DestroyOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver)
{
	if (Handle && Handle->IsValid())
	{
		auto& JointConstraints = InSolver->GetJointConstraints();
		JointConstraints.RemoveConstraint(Handle->GetConstraintIndex());

		delete Constraint;
		Constraint = nullptr;
	}
}


void FJointConstraintPhysicsProxy::PushStateOnGameThread(Chaos::FPBDRigidsSolver* InSolver)
{
	if (Constraint != nullptr && Constraint->IsValid())
	{
		if (Constraint->IsDirty())
		{
			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::CollisionEnabled))
			{
				JointSettingsBuffer.bCollisionEnabled = Constraint->GetCollisionEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::CollisionEnabled);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::Projection))
			{
				JointSettingsBuffer.bProjectionEnabled = Constraint->GetProjectionEnabled();
				JointSettingsBuffer.LinearProjection = Constraint->GetProjectionLinearAlpha();
				JointSettingsBuffer.AngularProjection = Constraint->GetProjectionAngularAlpha();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::Projection);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::ParentInvMassScale))
			{
				JointSettingsBuffer.ParentInvMassScale = Constraint->GetParentInvMassScale();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::ParentInvMassScale);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearBreakForce))
			{
				JointSettingsBuffer.LinearBreakForce = Constraint->GetLinearBreakForce();
				JointSettingsBuffer.LinearPlasticityLimit = Constraint->GetLinearPlasticityLimit();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearBreakForce);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::AngularBreakTorque))
			{
				JointSettingsBuffer.AngularBreakTorque = Constraint->GetAngularBreakTorque();
				JointSettingsBuffer.AngularPlasticityLimit = Constraint->GetAngularPlasticityLimit();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::AngularBreakTorque);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::UserData))
			{
				JointSettingsBuffer.UserData = Constraint->GetUserData();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::UserData);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearDrive))
			{
				JointSettingsBuffer.bLinearPositionDriveEnabled[0] = Constraint->GetLinearPositionDriveXEnabled();
				JointSettingsBuffer.bLinearPositionDriveEnabled[1] = Constraint->GetLinearPositionDriveYEnabled();
				JointSettingsBuffer.bLinearPositionDriveEnabled[2] = Constraint->GetLinearPositionDriveZEnabled();
				JointSettingsBuffer.LinearDrivePositionTarget = Constraint->GetLinearDrivePositionTarget();
				JointSettingsBuffer.bLinearVelocityDriveEnabled[0] = Constraint->GetLinearVelocityDriveXEnabled();
				JointSettingsBuffer.bLinearVelocityDriveEnabled[1] = Constraint->GetLinearVelocityDriveYEnabled();
				JointSettingsBuffer.bLinearVelocityDriveEnabled[2] = Constraint->GetLinearVelocityDriveZEnabled();
				JointSettingsBuffer.LinearDriveVelocityTarget = Constraint->GetLinearDriveVelocityTarget();
				JointSettingsBuffer.LinearDriveForceMode = Constraint->GetLinearDriveForceMode();
				JointSettingsBuffer.LinearMotionTypes[0] = Constraint->GetLinearMotionTypesX();
				JointSettingsBuffer.LinearMotionTypes[1] = Constraint->GetLinearMotionTypesY();
				JointSettingsBuffer.LinearMotionTypes[2] = Constraint->GetLinearMotionTypesZ();
				JointSettingsBuffer.LinearDriveStiffness = Constraint->GetLinearDriveStiffness();
				JointSettingsBuffer.LinearDriveDamping = Constraint->GetLinearDriveDamping();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearDrive);
			}


			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::AngularDrive))
			{
				JointSettingsBuffer.bAngularSLerpPositionDriveEnabled = Constraint->GetAngularSLerpPositionDriveEnabled();
				JointSettingsBuffer.bAngularTwistPositionDriveEnabled = Constraint->GetAngularTwistPositionDriveEnabled();
				JointSettingsBuffer.bAngularSwingPositionDriveEnabled = Constraint->GetAngularSwingPositionDriveEnabled();
				JointSettingsBuffer.AngularDrivePositionTarget = Constraint->GetAngularDrivePositionTarget();
				JointSettingsBuffer.bAngularSLerpVelocityDriveEnabled = Constraint->GetAngularSLerpVelocityDriveEnabled();
				JointSettingsBuffer.bAngularTwistVelocityDriveEnabled = Constraint->GetAngularTwistVelocityDriveEnabled();
				JointSettingsBuffer.bAngularSwingVelocityDriveEnabled = Constraint->GetAngularSwingVelocityDriveEnabled();
				JointSettingsBuffer.AngularDriveVelocityTarget = Constraint->GetAngularDriveVelocityTarget();
				JointSettingsBuffer.AngularDriveForceMode = Constraint->GetAngularDriveForceMode();
				JointSettingsBuffer.AngularMotionTypes[0] = Constraint->GetAngularMotionTypesX();
				JointSettingsBuffer.AngularMotionTypes[1] = Constraint->GetAngularMotionTypesY();
				JointSettingsBuffer.AngularMotionTypes[2] = Constraint->GetAngularMotionTypesZ();
				JointSettingsBuffer.AngularDriveStiffness = Constraint->GetAngularDriveStiffness();
				JointSettingsBuffer.AngularDriveDamping = Constraint->GetAngularDriveDamping();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::AngularDrive);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::Stiffness))
			{
				JointSettingsBuffer.Stiffness = Constraint->GetStiffness();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::Stiffness);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::Limits))
			{
				JointSettingsBuffer.bSoftLinearLimitsEnabled = Constraint->GetSoftLinearLimitsEnabled();
				JointSettingsBuffer.bSoftTwistLimitsEnabled = Constraint->GetSoftTwistLimitsEnabled();
				JointSettingsBuffer.bSoftSwingLimitsEnabled = Constraint->GetSoftSwingLimitsEnabled();
				JointSettingsBuffer.LinearSoftForceMode = Constraint->GetLinearSoftForceMode();
				JointSettingsBuffer.AngularSoftForceMode = Constraint->GetAngularSoftForceMode();
				JointSettingsBuffer.SoftLinearStiffness = Constraint->GetSoftLinearStiffness();
				JointSettingsBuffer.SoftLinearDamping = Constraint->GetSoftLinearDamping();
				JointSettingsBuffer.SoftTwistStiffness = Constraint->GetSoftTwistStiffness();
				JointSettingsBuffer.SoftTwistDamping = Constraint->GetSoftTwistDamping();
				JointSettingsBuffer.SoftSwingStiffness = Constraint->GetSoftSwingStiffness();
				JointSettingsBuffer.SoftSwingDamping = Constraint->GetSoftSwingDamping();
				JointSettingsBuffer.LinearLimit = Constraint->GetLinearLimit();
				JointSettingsBuffer.AngularLimits = Constraint->GetAngularLimits();
				JointSettingsBuffer.LinearContactDistance = Constraint->GetLinearContactDistance();
				JointSettingsBuffer.TwistContactDistance = Constraint->GetTwistContactDistance();
				JointSettingsBuffer.SwingContactDistance = Constraint->GetSwingContactDistance();
				JointSettingsBuffer.LinearRestitution = Constraint->GetLinearRestitution();
				JointSettingsBuffer.TwistRestitution = Constraint->GetTwistRestitution();
				JointSettingsBuffer.SwingRestitution = Constraint->GetSwingRestitution();

				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::Limits);
			}

			Constraint->ClearDirtyFlags();
		}
	}
}


void FJointConstraintPhysicsProxy::PushStateOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver)
{
	typedef typename Chaos::FPBDRigidsSolver::FPBDRigidsEvolution::FCollisionConstraints FCollisionConstraints;
	if (Handle && Handle->IsValid())
	{
		if (DirtyFlagsBuffer.IsDirty())
		{
			FConstraintData ConstraintSettings = Handle->GetSettings();

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::CollisionEnabled))
			{
				if (!JointSettingsBuffer.bCollisionEnabled)
				{
					Chaos::FConstraintBase::FProxyBasePair& BasePairs = Constraint->GetParticleProxies();
					Chaos::TGeometryParticleHandle<Chaos::FReal, 3>* Handle0 = GetParticleHandleFromProxy(BasePairs[0]);
					Chaos::TGeometryParticleHandle<Chaos::FReal, 3>* Handle1 = GetParticleHandleFromProxy(BasePairs[1]);

					// Three pieces of state to update on the physics thread. 
					// .. Mask on the particle array
					// .. Constraint collisions enabled array
					// .. IgnoreCollisionsManager
					if (Handle0 && Handle1)
					{
						Chaos::TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = Handle0->CastToRigidParticle();
						Chaos::TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = Handle1->CastToRigidParticle();

						if (ParticleHandle0 && ParticleHandle1)
						{
							Chaos::FIgnoreCollisionManager& IgnoreCollisionManager = InSolver->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();
							Chaos::FUniqueIdx ID0 = ParticleHandle0->UniqueIdx();
							Chaos::FUniqueIdx ID1 = ParticleHandle1->UniqueIdx();


							ParticleHandle0->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
							IgnoreCollisionManager.AddIgnoreCollisionsFor(ID0, ID1);

							ParticleHandle1->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
							IgnoreCollisionManager.AddIgnoreCollisionsFor(ID1, ID0);
							ConstraintSettings.bCollisionEnabled = JointSettingsBuffer.bCollisionEnabled;
						}
					}
				}
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::Projection))
			{
				ConstraintSettings.bProjectionEnabled = JointSettingsBuffer.bProjectionEnabled;
				ConstraintSettings.LinearProjection = JointSettingsBuffer.LinearProjection;
				ConstraintSettings.AngularProjection = JointSettingsBuffer.AngularProjection;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::ParentInvMassScale))
			{
				ConstraintSettings.ParentInvMassScale = JointSettingsBuffer.ParentInvMassScale;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearBreakForce))
			{
				ConstraintSettings.LinearBreakForce = JointSettingsBuffer.LinearBreakForce;
				ConstraintSettings.LinearPlasticityLimit = FMath::Clamp((float)JointSettingsBuffer.LinearPlasticityLimit, 0.f, 1.f);
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::AngularBreakTorque))
			{
				ConstraintSettings.AngularBreakTorque = JointSettingsBuffer.AngularBreakTorque;
				ConstraintSettings.AngularPlasticityLimit = JointSettingsBuffer.AngularPlasticityLimit;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::UserData))
			{
				ConstraintSettings.UserData = JointSettingsBuffer.UserData;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearDrive))
			{
				ConstraintSettings.bLinearPositionDriveEnabled[0] = JointSettingsBuffer.bLinearPositionDriveEnabled[0];
				ConstraintSettings.bLinearPositionDriveEnabled[1] = JointSettingsBuffer.bLinearPositionDriveEnabled[1];
				ConstraintSettings.bLinearPositionDriveEnabled[2] = JointSettingsBuffer.bLinearPositionDriveEnabled[2];
				ConstraintSettings.LinearDrivePositionTarget = JointSettingsBuffer.LinearDrivePositionTarget;
				ConstraintSettings.bLinearVelocityDriveEnabled[0] = JointSettingsBuffer.bLinearVelocityDriveEnabled[0];
				ConstraintSettings.bLinearVelocityDriveEnabled[1] = JointSettingsBuffer.bLinearVelocityDriveEnabled[1];
				ConstraintSettings.bLinearVelocityDriveEnabled[2] = JointSettingsBuffer.bLinearVelocityDriveEnabled[2];
				ConstraintSettings.LinearDriveVelocityTarget = JointSettingsBuffer.LinearDriveVelocityTarget;
				ConstraintSettings.LinearDriveForceMode = JointSettingsBuffer.LinearDriveForceMode;
				ConstraintSettings.LinearMotionTypes[0] = JointSettingsBuffer.LinearMotionTypes[0];
				ConstraintSettings.LinearMotionTypes[1] = JointSettingsBuffer.LinearMotionTypes[1];
				ConstraintSettings.LinearMotionTypes[2] = JointSettingsBuffer.LinearMotionTypes[2];
				ConstraintSettings.LinearDriveStiffness = JointSettingsBuffer.LinearDriveStiffness;
				ConstraintSettings.LinearDriveDamping = JointSettingsBuffer.LinearDriveDamping;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::AngularDrive))
			{
				ConstraintSettings.bAngularSLerpPositionDriveEnabled = JointSettingsBuffer.bAngularSLerpPositionDriveEnabled;
				ConstraintSettings.bAngularTwistPositionDriveEnabled = JointSettingsBuffer.bAngularTwistPositionDriveEnabled;
				ConstraintSettings.bAngularSwingPositionDriveEnabled = JointSettingsBuffer.bAngularSwingPositionDriveEnabled;
				ConstraintSettings.AngularDrivePositionTarget = JointSettingsBuffer.AngularDrivePositionTarget;
				ConstraintSettings.bAngularSLerpVelocityDriveEnabled = JointSettingsBuffer.bAngularSLerpVelocityDriveEnabled;
				ConstraintSettings.bAngularTwistVelocityDriveEnabled = JointSettingsBuffer.bAngularTwistVelocityDriveEnabled;
				ConstraintSettings.bAngularSwingVelocityDriveEnabled = JointSettingsBuffer.bAngularSwingVelocityDriveEnabled;
				ConstraintSettings.AngularDriveVelocityTarget = JointSettingsBuffer.AngularDriveVelocityTarget;
				ConstraintSettings.AngularDriveForceMode = JointSettingsBuffer.AngularDriveForceMode;
				ConstraintSettings.AngularMotionTypes[0] = JointSettingsBuffer.AngularMotionTypes[0];
				ConstraintSettings.AngularMotionTypes[1] = JointSettingsBuffer.AngularMotionTypes[1];
				ConstraintSettings.AngularMotionTypes[2] = JointSettingsBuffer.AngularMotionTypes[2];
				ConstraintSettings.AngularDriveStiffness = JointSettingsBuffer.AngularDriveStiffness;
				ConstraintSettings.AngularDriveDamping = JointSettingsBuffer.AngularDriveDamping;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::Stiffness))
			{
				ConstraintSettings.Stiffness = JointSettingsBuffer.Stiffness;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::Limits))
			{
				ConstraintSettings.bSoftLinearLimitsEnabled = JointSettingsBuffer.bSoftLinearLimitsEnabled;
				ConstraintSettings.bSoftTwistLimitsEnabled = JointSettingsBuffer.bSoftTwistLimitsEnabled;
				ConstraintSettings.bSoftSwingLimitsEnabled = JointSettingsBuffer.bSoftSwingLimitsEnabled;
				ConstraintSettings.LinearSoftForceMode = JointSettingsBuffer.LinearSoftForceMode;
				ConstraintSettings.AngularSoftForceMode = JointSettingsBuffer.AngularSoftForceMode;
				ConstraintSettings.SoftLinearStiffness = JointSettingsBuffer.SoftLinearStiffness;
				ConstraintSettings.SoftLinearDamping = JointSettingsBuffer.SoftLinearDamping;
				ConstraintSettings.SoftTwistStiffness = JointSettingsBuffer.SoftTwistStiffness;
				ConstraintSettings.SoftTwistDamping = JointSettingsBuffer.SoftTwistDamping;
				ConstraintSettings.SoftSwingStiffness = JointSettingsBuffer.SoftSwingStiffness;
				ConstraintSettings.SoftSwingDamping = JointSettingsBuffer.SoftSwingDamping;
				ConstraintSettings.LinearLimit = JointSettingsBuffer.LinearLimit;
				ConstraintSettings.AngularLimits = JointSettingsBuffer.AngularLimits;
				ConstraintSettings.LinearContactDistance = JointSettingsBuffer.LinearContactDistance;
				ConstraintSettings.TwistContactDistance = JointSettingsBuffer.TwistContactDistance;
				ConstraintSettings.SwingContactDistance = JointSettingsBuffer.SwingContactDistance;
				ConstraintSettings.LinearRestitution = JointSettingsBuffer.LinearRestitution;
				ConstraintSettings.TwistRestitution = JointSettingsBuffer.TwistRestitution;
				ConstraintSettings.SwingRestitution = JointSettingsBuffer.SwingRestitution;
			}

			Handle->SetSettings(ConstraintSettings);

			DirtyFlagsBuffer.Clear();
		}
	}
}
