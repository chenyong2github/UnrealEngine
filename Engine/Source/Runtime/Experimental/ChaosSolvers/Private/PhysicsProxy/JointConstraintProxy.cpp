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
					Handle->SetSettings(JointSettingsBuffer);
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

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::ProjectionEnabled))
			{
				JointSettingsBuffer.bProjectionEnabled = Constraint->GetProjectionEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::ProjectionEnabled);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::ParentInvMassScale))
			{
				JointSettingsBuffer.ParentInvMassScale = Constraint->GetParentInvMassScale();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::ParentInvMassScale);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearBreakForce))
			{
				JointSettingsBuffer.LinearBreakForce = Constraint->GetLinearBreakForce();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearBreakForce);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::AngularBreakTorque))
			{
				JointSettingsBuffer.AngularBreakTorque = Constraint->GetAngularBreakTorque();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::AngularBreakTorque);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::UserData))
			{
				JointSettingsBuffer.UserData = Constraint->GetUserData();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::UserData);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearPositionDriveXEnabled))
			{
				JointSettingsBuffer.bLinearPositionDriveEnabled[0] = Constraint->GetLinearPositionDriveXEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearPositionDriveXEnabled);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearPositionDriveYEnabled))
			{
				JointSettingsBuffer.bLinearPositionDriveEnabled[1] = Constraint->GetLinearPositionDriveYEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearPositionDriveYEnabled);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearPositionDriveZEnabled))
			{
				JointSettingsBuffer.bLinearPositionDriveEnabled[2] = Constraint->GetLinearPositionDriveZEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearPositionDriveZEnabled);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearDrivePositionTarget))
			{
				JointSettingsBuffer.LinearDrivePositionTarget = Constraint->GetLinearDrivePositionTarget();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearDrivePositionTarget);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveXEnabled))
			{
				JointSettingsBuffer.bLinearVelocityDriveEnabled[0] = Constraint->GetLinearVelocityDriveXEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveXEnabled);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveYEnabled))
			{
				JointSettingsBuffer.bLinearVelocityDriveEnabled[1] = Constraint->GetLinearVelocityDriveYEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveYEnabled);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveZEnabled))
			{
				JointSettingsBuffer.bLinearVelocityDriveEnabled[2] = Constraint->GetLinearVelocityDriveZEnabled();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveZEnabled);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearDriveVelocityTarget))
			{
				JointSettingsBuffer.LinearDriveVelocityTarget = Constraint->GetLinearDriveVelocityTarget();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearDriveVelocityTarget);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearDriveForceMode))
			{
				JointSettingsBuffer.LinearDriveForceMode = Constraint->GetLinearDriveForceMode();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearDriveForceMode);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearMotionTypesX))
			{
				JointSettingsBuffer.LinearMotionTypes[0] = Constraint->GetLinearMotionTypesX();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearMotionTypesX);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearMotionTypesY))
			{
				JointSettingsBuffer.LinearMotionTypes[1] = Constraint->GetLinearMotionTypesY();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearMotionTypesY);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearMotionTypesY))
			{
				JointSettingsBuffer.LinearMotionTypes[2] = Constraint->GetLinearMotionTypesY();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearMotionTypesY);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearLimit))
			{
				JointSettingsBuffer.LinearLimit = Constraint->GetLinearLimit();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearLimit);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearDriveStiffness))
			{
				JointSettingsBuffer.LinearDriveStiffness = Constraint->GetLinearDriveStiffness();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearDriveStiffness);
			}

			if (Constraint->IsDirty(Chaos::EJointConstraintFlags::LinearDriveDamping))
			{
				JointSettingsBuffer.LinearDriveDamping = Constraint->GetLinearDriveDamping();
				DirtyFlagsBuffer.MarkDirty(Chaos::EJointConstraintFlags::LinearDriveDamping);
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
	check(Handle != nullptr);
	if (DirtyFlagsBuffer.IsDirty())
	{
		FConstraintData& ConstraintSettings = Handle->GetSettings();
		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::CollisionEnabled))
		{
			auto Particles = Constraint->GetJointParticles();

			// Three pieces of state to update on the physics thread. 
			// .. Mask on the particle array
			// .. Constraint collisions enabled array
			// .. IgnoreCollisionsManager
			if (Particles[0]->Handle() && Particles[1]->Handle())
			{
				Chaos::TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = Particles[0]->Handle()->CastToRigidParticle();
				Chaos::TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = Particles[1]->Handle()->CastToRigidParticle();

				if (ParticleHandle0 && ParticleHandle1)
				{
					Chaos::FIgnoreCollisionManager& IgnoreCollisionManager = InSolver->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();
					Chaos::FUniqueIdx ID0 = ParticleHandle0->UniqueIdx();
					Chaos::FUniqueIdx ID1 = ParticleHandle1->UniqueIdx();

					
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
						ParticleHandle0->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
						IgnoreCollisionManager.AddIgnoreCollisionsFor(ID0, ID1);

						ParticleHandle1->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
						IgnoreCollisionManager.AddIgnoreCollisionsFor(ID1, ID0);
					}
					ConstraintSettings.bCollisionEnabled = JointSettingsBuffer.bCollisionEnabled;
				}
			}
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::ProjectionEnabled))
		{
			ConstraintSettings.bProjectionEnabled = JointSettingsBuffer.bProjectionEnabled;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::ParentInvMassScale))
		{
			ConstraintSettings.ParentInvMassScale = JointSettingsBuffer.ParentInvMassScale;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearBreakForce))
		{
			ConstraintSettings.LinearBreakForce = JointSettingsBuffer.LinearBreakForce;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::AngularBreakTorque))
		{
			ConstraintSettings.AngularBreakTorque = JointSettingsBuffer.AngularBreakTorque;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::UserData))
		{
			ConstraintSettings.UserData = JointSettingsBuffer.UserData;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearPositionDriveXEnabled))
		{
			ConstraintSettings.bLinearPositionDriveEnabled[0] = JointSettingsBuffer.bLinearPositionDriveEnabled[0];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearPositionDriveYEnabled))
		{
			ConstraintSettings.bLinearPositionDriveEnabled[1] = JointSettingsBuffer.bLinearPositionDriveEnabled[1];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearPositionDriveZEnabled))
		{
			ConstraintSettings.bLinearPositionDriveEnabled[2] = JointSettingsBuffer.bLinearPositionDriveEnabled[2];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearDrivePositionTarget))
		{
			ConstraintSettings.LinearDrivePositionTarget = JointSettingsBuffer.LinearDrivePositionTarget;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveXEnabled))
		{
			ConstraintSettings.bLinearVelocityDriveEnabled[0] = JointSettingsBuffer.bLinearVelocityDriveEnabled[0];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveYEnabled))
		{
			ConstraintSettings.bLinearVelocityDriveEnabled[1] = JointSettingsBuffer.bLinearVelocityDriveEnabled[1];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearVelocityDriveZEnabled))
		{
			ConstraintSettings.bLinearVelocityDriveEnabled[2] = JointSettingsBuffer.bLinearVelocityDriveEnabled[2];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearDriveVelocityTarget))
		{
			ConstraintSettings.LinearDriveVelocityTarget = JointSettingsBuffer.LinearDriveVelocityTarget;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearDriveForceMode))
		{
			ConstraintSettings.LinearDriveForceMode = JointSettingsBuffer.LinearDriveForceMode;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearMotionTypesX))
		{
			ConstraintSettings.LinearMotionTypes[0] = JointSettingsBuffer.LinearMotionTypes[0];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearMotionTypesY))
		{
			ConstraintSettings.LinearMotionTypes[1] = JointSettingsBuffer.LinearMotionTypes[1];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearMotionTypesZ))
		{
			ConstraintSettings.LinearMotionTypes[2] = JointSettingsBuffer.LinearMotionTypes[2];
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearLimit))
		{
			ConstraintSettings.LinearLimit = JointSettingsBuffer.LinearLimit;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearDriveStiffness))
		{
			ConstraintSettings.LinearDriveStiffness = JointSettingsBuffer.LinearDriveStiffness;
		}

		if (DirtyFlagsBuffer.IsDirty(Chaos::EJointConstraintFlags::LinearDriveDamping))
		{
			ConstraintSettings.LinearDriveDamping = JointSettingsBuffer.LinearDriveDamping;
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