// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/JointConstraintProxy.h"

#include "ChaosStats.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Serializable.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "PhysicsSolver.h"

template< class CONSTRAINT_TYPE >
TJointConstraintProxy<CONSTRAINT_TYPE>::TJointConstraintProxy(CONSTRAINT_TYPE* InConstraint, TJointConstraintProxy<CONSTRAINT_TYPE>::FConstraintHandle* InHandle, UObject* InOwner, Chaos::FPBDJointSettings InInitialState)
	: Base(InOwner)
	, InitialState(InInitialState)
	, Constraint(InConstraint)
	, Handle(InHandle)
{}


template< class CONSTRAINT_TYPE >
TJointConstraintProxy<CONSTRAINT_TYPE>::~TJointConstraintProxy()
{
}

template < >
template < >
void TJointConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FNonRewindableEvolutionTraits>* InSolver)
{
	// @todo(JointConstraint): Add a constraint
}


template < >
template < >
void TJointConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FNonRewindableEvolutionTraits>* RBDSolver)
{
	// @todo(JointConstraint): Remove a constraint
}

template < >
template < >
void TJointConstraintProxy<Chaos::FJointConstraint>::InitializeOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FRewindableEvolutionTraits>* InSolver)
{
	// @todo(chaos) : Implement
}


template < >
template < >
void TJointConstraintProxy<Chaos::FJointConstraint>::DestroyOnPhysicsThread(Chaos::TPBDRigidsSolver<Chaos::FRewindableEvolutionTraits>* RBDSolver)
{
	// @todo(chaos) : Implement
}


template<>
EPhysicsProxyType TJointConstraintProxy<Chaos::FJointConstraint>::ConcreteType()
{ 
	return EPhysicsProxyType::JointConstraintType;
}



template class TJointConstraintProxy< Chaos::FJointConstraint >;

