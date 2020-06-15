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
#include "PhysicsSolver.h"

template< class CONSTRAINT_TYPE >
TJointConstraintProxy<CONSTRAINT_TYPE>::TJointConstraintProxy(CONSTRAINT_TYPE* InConstraint, TJointConstraintProxy<CONSTRAINT_TYPE>::FConstraintHandle* InHandle, UObject* InOwner)
	: Base(InOwner)
	, Constraint(InConstraint)
	, Handle(InHandle)
	, bInitialized(false)
{
	check(Constraint!=nullptr);
	Constraint->SetProxy(this);
	JointSettingsBuffer = Constraint->GetJointSettings();
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
		if (Constraint->IsDirty())
		{
			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::CollisionEnabled))
			{
				JointSettingsBuffer.bCollisionEnabled = Constraint->GetCollisionEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::CollisionEnabled);
			}


			Constraint->ClearDirtyFlags();
		}
	}
}


template < >
template < class Trait >
void TJointConstraintProxy<Chaos::FJointConstraint>::PushStateOnPhysicsThread(Chaos::TPBDRigidsSolver<Trait>* InSolver)
{
	typedef typename Chaos::TPBDRigidsSolver<Trait>::FPBDRigidsEvolution::FCollisionConstraints FCollisionConstraints;
	if (DirtyFlagsBuffer.IsDirty())
	{
		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::CollisionEnabled))
		{
			auto Particles = Constraint->GetJointParticles();

			if (Particles[0]->Handle() && Particles[1]->Handle())
			{
				Chaos::TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = Particles[0]->Handle()->CastToRigidParticle();
				Chaos::TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = Particles[1]->Handle()->CastToRigidParticle();

				if (ParticleHandle0 && ParticleHandle1)
				{
					Chaos::FIgnoreCollisionManager& IgnoreCollisionManager = InSolver->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();
					Chaos::FParticleID ID0 = ParticleHandle0->ParticleID();
					Chaos::FParticleID ID1 = ParticleHandle1->ParticleID();

					
					if (JointSettingsBuffer.bCollisionEnabled)
					{
						IgnoreCollisionManager.RemoveIgnoreCollisionsFor(ID0, ID1);
						if (IgnoreCollisionManager.NumIgnoredCollision(ID0))
						{
							ParticleHandle0->RemoveCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
						}

						IgnoreCollisionManager.RemoveIgnoreCollisionsFor(ID1, ID0);
						if (IgnoreCollisionManager.NumIgnoredCollision(ID1))
						{
							ParticleHandle1->RemoveCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
						}
					}
					else
					{
						// @todo(brice) : Make this a mask...
						ParticleHandle1->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
						IgnoreCollisionManager.AddIgnoreCollisionsFor(ID0, ID1);

						ParticleHandle1->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
						IgnoreCollisionManager.AddIgnoreCollisionsFor(ID1, ID0);
					}
					
				}
			}
		}
		DirtyFlagsBuffer.Clear();
	}
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