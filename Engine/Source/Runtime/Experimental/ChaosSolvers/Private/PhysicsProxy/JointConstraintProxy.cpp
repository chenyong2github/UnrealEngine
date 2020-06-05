// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/JointConstraintProxy.h"

#include "ChaosStats.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Serializable.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "PhysicsSolver.h"

template< class CONSTRAINT_TYPE >
TJointConstraintProxy<CONSTRAINT_TYPE>::TJointConstraintProxy(CONSTRAINT_TYPE* InConstraint, TJointConstraintProxy<CONSTRAINT_TYPE>::FConstraintHandle* InHandle, UObject* InOwner)
	: Base(InOwner)
	, Constraint(InConstraint)
	, Handle(InHandle)
	, bInitialized(false)
{
	Constraint->SetProxy(this);
}


template< class CONSTRAINT_TYPE >
TJointConstraintProxy<CONSTRAINT_TYPE>::~TJointConstraintProxy()
{
}



template < >
template < class Traits>
void TJointConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Traits>* InSolver)
{
	auto& Handles = InSolver->GetParticles().GetParticleHandles();
	if (Handles.Size() && IsValid())
	{
		auto& JointConstraints = InSolver->GetJointConstraints();
		if (Constraint != nullptr)
		{
			auto Particles = Constraint->GetJointParticles();
			if (Particles[0] && Particles[0]->Handle())
			{
				if (Particles[1] && Particles[1]->Handle())
				{
					Handle = JointConstraints.AddConstraint({ Particles[0]->Handle() , Particles[1]->Handle() }, Constraint->GetJointTransforms());
				}
			}
		}
	}
}

template < >
template < class Traits>
void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnGameThread()
{
	if (Constraint != nullptr)
	{
		Constraint->ClearDirtyFlags();
	}
}

template < >
template < class Traits>
void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnPhysicsThread()
{
}

template < >
template < class Traits>
void TJointConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Traits>* RBDSolver)
{
	// @todo(chaos) : Implement
}


template<>
EPhysicsProxyType TJointConstraintProxy<Chaos::FJointConstraint>::ConcreteType()
{ 
	return EPhysicsProxyType::JointConstraintType;
}


// Template specializations defined against Chaos::Traits. 
template void TJointConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FNonRewindableEvolutionTraits>* InSolver);
template void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnGameThread< Chaos::FNonRewindableEvolutionTraits>();
template void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnPhysicsThread< Chaos::FNonRewindableEvolutionTraits>();
template void TJointConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FNonRewindableEvolutionTraits>* RBDSolver);

template void TJointConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FRewindableEvolutionTraits>* InSolver);
template void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnGameThread< Chaos::FRewindableEvolutionTraits>();
template void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnPhysicsThread< Chaos::FRewindableEvolutionTraits>();
template void TJointConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FRewindableEvolutionTraits>* RBDSolver);

template class TJointConstraintProxy< Chaos::FJointConstraint >;

