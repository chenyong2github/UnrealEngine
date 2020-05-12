// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/JointConstraintProxy.h"

#include "ChaosStats.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Serializable.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "PhysicsSolver.h"

template< class CONSTRAINT_TYPE >
FConstraintProxy<CONSTRAINT_TYPE>::FConstraintProxy(CONSTRAINT_TYPE* InConstraint, FConstraintProxy<CONSTRAINT_TYPE>::FConstraintHandle* InHandle, UObject* InOwner, Chaos::FPBDJointSettings InInitialState)
	: Base(InOwner)
	, InitialState(InInitialState)
	, Constraint(InConstraint)
	, Handle(InHandle)
{}


template< class CONSTRAINT_TYPE >
FConstraintProxy<CONSTRAINT_TYPE>::~FConstraintProxy()
{
}

template < >
template < >
void FConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FNonRewindableEvolutionTraits>* InSolver)
{
	// @todo(JointConstraint): Add a constraint
}


template < >
template < >
void FConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FNonRewindableEvolutionTraits>* RBDSolver)
{
	// @todo(JointConstraint): Remove a constraint
}

template < >
template < >
void FConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FRewindableEvolutionTraits>* InSolver)
{
	// @todo(chaos) : Implement
}


template < >
template < >
void FConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FRewindableEvolutionTraits>* RBDSolver)
{
	// @todo(chaos) : Implement
}


template<>
EPhysicsProxyType FConstraintProxy<Chaos::FJointConstraint>::ConcreteType()
{ 
	return EPhysicsProxyType::JointConstraintType;
}



template class FConstraintProxy< Chaos::FJointConstraint >;

