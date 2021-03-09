// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/SuspensionConstraintProxy.h"

#include "ChaosStats.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/Collision/CollisionConstraintFlags.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/Serializable.h"
#include "Chaos/PBDSuspensionConstraints.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsSolver.h"



FSuspensionConstraintPhysicsProxy::FSuspensionConstraintPhysicsProxy(Chaos::FSuspensionConstraint* InConstraint, FConstraintHandle* InHandle, UObject* InOwner)
	: Base(InOwner)
	, Constraint(InConstraint)
	, Handle(InHandle)
	, bInitialized(false)
{
	check(Constraint!=nullptr);
	Constraint->SetProxy(this);
	SuspensionSettingsBuffer = Constraint->GetSuspensionSettings();
}

Chaos::TGeometryParticleHandle<Chaos::FReal, 3>*
FSuspensionConstraintPhysicsProxy::GetParticleHandleFromProxy(IPhysicsProxyBase* ProxyBase)
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

void FSuspensionConstraintPhysicsProxy::InitializeOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver)
{
	auto& Handles = InSolver->GetParticles().GetParticleHandles();
	if (Handles.Size() && IsValid())
	{
		auto& SuspensionConstraints = InSolver->GetSuspensionConstraints();
		if (Constraint != nullptr)
		{
			Chaos::FConstraintBase::FProxyBasePair& BasePairs = Constraint->GetParticleProxies();

			
			Chaos::TGeometryParticleHandle<Chaos::FReal, 3>* Handle0 = GetParticleHandleFromProxy(BasePairs[0]);
			if (Handle0)
			{
				Handle = SuspensionConstraints.AddConstraint(Handle0, Constraint->GetLocation()
					, SuspensionSettingsBuffer);
			}
			
		}
	}
}


void FSuspensionConstraintPhysicsProxy::PushStateOnGameThread(Chaos::FPBDRigidsSolver* InSolver)
{
	if (Constraint != nullptr)
	{
		if (Constraint->IsDirty())
		{
			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::Enabled))
			{
				SuspensionSettingsBuffer.Enabled = Constraint->GetEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::Enabled);
			}

			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::Target))
			{
				SuspensionSettingsBuffer.Target = Constraint->GetTarget();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::Target);
			}

			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::HardstopStiffness))
			{
				SuspensionSettingsBuffer.HardstopStiffness = Constraint->GetHardstopStiffness();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::HardstopStiffness);
			}

			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::HardstopVelocityCompensation))
			{
				SuspensionSettingsBuffer.HardstopVelocityCompensation = Constraint->GetHardstopVelocityCompensation();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::HardstopVelocityCompensation);
			}

			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::SpringPreload))
			{
				SuspensionSettingsBuffer.SpringPreload = Constraint->GetSpringPreload();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::SpringPreload);
			}

			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::SpringStiffness))
			{
				SuspensionSettingsBuffer.SpringStiffness = Constraint->GetSpringStiffness();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::SpringStiffness);
			}

			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::SpringDamping))
			{
				SuspensionSettingsBuffer.SpringDamping = Constraint->GetSpringDamping();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::SpringDamping);
			}

			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::MinLength))
			{
				SuspensionSettingsBuffer.MinLength = Constraint->GetMinLength();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::MinLength);
			}

			if (Constraint->IsDirty(Chaos::ESuspensionConstraintFlags::MaxLength))
			{
				SuspensionSettingsBuffer.MaxLength = Constraint->GetMaxLength();
				DirtyFlagsBuffer.MarkDirty(Chaos::ESuspensionConstraintFlags::MaxLength);
			}

			Constraint->ClearDirtyFlags();
		}
	}
}


void FSuspensionConstraintPhysicsProxy::PushStateOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver)
{
	typedef typename Chaos::FPBDRigidsSolver::FPBDRigidsEvolution::FCollisionConstraints FCollisionConstraints;
	if (Handle)
	{
		if (DirtyFlagsBuffer.IsDirty())
		{
			FConstraintData& ConstraintSettings = Handle->GetSettings();

			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::Enabled))
			{
				ConstraintSettings.Enabled = SuspensionSettingsBuffer.Enabled;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::Target))
			{
				ConstraintSettings.Target = SuspensionSettingsBuffer.Target;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::HardstopStiffness))
			{
				ConstraintSettings.HardstopStiffness = SuspensionSettingsBuffer.HardstopStiffness;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::HardstopVelocityCompensation))
			{
				ConstraintSettings.HardstopVelocityCompensation = SuspensionSettingsBuffer.HardstopVelocityCompensation;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::SpringPreload))
			{
				ConstraintSettings.SpringPreload = SuspensionSettingsBuffer.SpringPreload;
			}
			
			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::SpringStiffness))
			{
				ConstraintSettings.SpringStiffness = SuspensionSettingsBuffer.SpringStiffness;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::SpringDamping))
			{
				ConstraintSettings.SpringDamping = SuspensionSettingsBuffer.SpringDamping;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::MinLength))
			{
				ConstraintSettings.MinLength = SuspensionSettingsBuffer.MinLength;
			}

			if (DirtyFlagsBuffer.IsDirty(Chaos::ESuspensionConstraintFlags::MaxLength))
			{
				ConstraintSettings.MaxLength = SuspensionSettingsBuffer.MaxLength;
			}

			DirtyFlagsBuffer.Clear();
		}
	}
}


void FSuspensionConstraintPhysicsProxy::DestroyOnPhysicsThread(Chaos::FPBDRigidsSolver* RBDSolver)
{
	if (Handle)
	{
		auto& SuspensionConstraints = RBDSolver->GetSuspensionConstraints();
		SuspensionConstraints.RemoveConstraint(Handle->GetConstraintIndex());

		delete Constraint; 
		Constraint = nullptr;
	}
}
