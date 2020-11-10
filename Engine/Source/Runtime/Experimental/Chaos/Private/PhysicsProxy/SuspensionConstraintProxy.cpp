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
#include "PhysicsSolver.h"


template< class CONSTRAINT_TYPE >
TSuspensionConstraintProxy<CONSTRAINT_TYPE>::TSuspensionConstraintProxy(CONSTRAINT_TYPE* InConstraint, TSuspensionConstraintProxy<CONSTRAINT_TYPE>::FConstraintHandle* InHandle, UObject* InOwner)
	: Base(InOwner)
	, Constraint(InConstraint)
	, Handle(InHandle)
	, bInitialized(false)
{
	check(Constraint!=nullptr);
	Constraint->SetProxy(this);
	SuspensionSettingsBuffer = Constraint->GetSuspensionSettings();
}


template< class CONSTRAINT_TYPE >
TSuspensionConstraintProxy<CONSTRAINT_TYPE>::~TSuspensionConstraintProxy()
{
}


template< class CONSTRAINT_TYPE>
EPhysicsProxyType TSuspensionConstraintProxy<CONSTRAINT_TYPE>::ConcreteType()
{
	return EPhysicsProxyType::NoneType;
}


template<>
EPhysicsProxyType TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::ConcreteType()
{
	return EPhysicsProxyType::SuspensionConstraintType;
}


template < >
template < class Trait >
void TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Trait>* InSolver)
{
	auto& Handles = InSolver->GetParticles().GetParticleHandles();
	if (Handles.Size() && IsValid())
	{
		auto& SuspensionConstraints = InSolver->GetSuspensionConstraints();
		if (Constraint != nullptr)
		{
			auto Particles = Constraint->GetParticles();

			if (Particles[0] && Particles[0]->Handle())
			{
				Handle = SuspensionConstraints.AddConstraint( Particles[0]->Handle(), Constraint->GetLocation()
					, SuspensionSettingsBuffer);
			}
		}
	}
}


template < >
template < class Trait >
void TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::PushStateOnGameThread(Chaos::TPBDRigidsSolver<Trait>* InSolver)
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


template < >
template < class Trait >
void TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::PushStateOnPhysicsThread(Chaos::TPBDRigidsSolver<Trait>* InSolver)
{
	typedef typename Chaos::TPBDRigidsSolver<Trait>::FPBDRigidsEvolution::FCollisionConstraints FCollisionConstraints;
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


template < >
template < class Trait >
void TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Trait>* RBDSolver)
{
	if (Handle)
	{
		auto& SuspensionConstraints = RBDSolver->GetSuspensionConstraints();
		SuspensionConstraints.RemoveConstraint(Handle->GetConstraintIndex());
	}
}


template class TSuspensionConstraintProxy< Chaos::FSuspensionConstraint >;

#define EVOLUTION_TRAIT(Traits)\
template void TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
template void TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::PushStateOnGameThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
template void TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::PushStateOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
template void TSuspensionConstraintProxy<Chaos::FSuspensionConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* RBDSolver);\

#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT

