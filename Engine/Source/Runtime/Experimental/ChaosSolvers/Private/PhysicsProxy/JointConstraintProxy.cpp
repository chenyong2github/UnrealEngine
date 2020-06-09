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



template< class CONSTRAINT_TYPE>
EPhysicsProxyType TJointConstraintProxy<CONSTRAINT_TYPE>::ConcreteType()
{
	return EPhysicsProxyType::NoneType;
}

template<>
EPhysicsProxyType TJointConstraintProxy<Chaos::FJointConstraint>::ConcreteType()
{
	return EPhysicsProxyType::JointConstraintType;
}



template < >
template < class Trait >
void TJointConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Trait>* InSolver)
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
template < class Trait >
void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnGameThread(Chaos::TPBDRigidsSolver<Trait>* InSolver)
{
	if (Constraint != nullptr)
	{
		Constraint->ClearDirtyFlags();
	}
}


template < >
template < class Trait >
void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnPhysicsThread(Chaos::TPBDRigidsSolver<Trait>* InSolver)
{
}


template < >
template < class Trait >
void TJointConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Trait>* RBDSolver)
{
	// @todo(chaos) : Implement
}



template class TJointConstraintProxy< Chaos::FJointConstraint >;

#define EVOLUTION_TRAIT(Traits)\
template void TJointConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
template void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnGameThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
template void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
template void TJointConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::Traits>* RBDSolver);\

#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT