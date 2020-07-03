// Copyright Epic Games, Inc. All Rights Reserved.

#include "MockPhysicsSimulation.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Chaos/ParticleHandle.h"

// --------------------------------------------------------------------------------------------------------------
//	Super simple mock example of a NP simulation running on top of physics
// --------------------------------------------------------------------------------------------------------------

namespace MockPhysics
{
	// This stuff is lifted directly from the physics code. It is unclear if this is an OK path to go down or if we need to
	// interact with the FBodyInstance instead of the FPhysicsActorHandle. (And if this is OK, how do we avoid duplicating the 
	// entire physics API).
	//
	// An ideal world is NP works directly with the lowest level physics types possible and we only push to the unreal layer
	// at the end of the (physics) frame. Obviously lots of data and settings are controlled by the unreal layer but for the
	// inner resimmable loop, removing jumps to unreal are desired.
	//
	// This is true even for non physics sims. We desire a kinematic character mover sim to talk to the lowest level physics
	// layer as possible instead of having to route even basic stuff through UPrimitiveComponent::MoveComponentImpl for example.
	
	bool IsRigidBodyKinematic_AssumesLocked(const FPhysicsActorHandle& InActorRef)
	{
		if(FPhysicsInterface::IsRigidBody(InActorRef))
		{
			return FPhysicsInterface::IsKinematic_AssumesLocked(InActorRef);
		}

		return false;
	}
		
	FChaosScene* GetPhysicsScene(const FPhysicsActorHandle& ActorHandle)
	{
		return FPhysicsInterface::GetCurrentScene(ActorHandle);
	}

	void AddForce_AssumesLocked(const FPhysicsActorHandle& ActorHandle, const FVector& Force, bool bAllowSubstepping, bool bAccelChange)
	{
		Chaos::TPBDRigidParticle<float, 3>* Rigid = ActorHandle->CastToRigidParticle();
		if(Rigid)
		{
			Chaos::EObjectStateType ObjectState = Rigid->ObjectState();
			
			if (npEnsure(ObjectState == Chaos::EObjectStateType::Dynamic || ObjectState == Chaos::EObjectStateType::Sleeping))
			{
				Rigid->SetObjectState(Chaos::EObjectStateType::Dynamic);

				const Chaos::TVector<float, 3> CurrentForce = Rigid->F();
				if (bAccelChange)
				{
					const float Mass = Rigid->M();
					const Chaos::TVector<float, 3> TotalAcceleration = CurrentForce + (Force * Mass);
					Rigid->SetF(TotalAcceleration);
				}
				else
				{
					Rigid->SetF(CurrentForce + Force);
				}
			}
		}
	}
}

// -------------------------------------------------------

void FMockPhysicsSimulation::SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<MockPhysicsStateTypes>& Input, const TNetSimOutput<MockPhysicsStateTypes>& Output)
{
	npCheckSlow(this->PhysicsActorHandle);

	const bool bAllowSubstepping = false;
	const bool bAccelChange = true;

	FVector Force = Input.Cmd->MovementInput;
	Force *= (1000.f * Input.Aux->ForceMultiplier);

	// Apply constant force based on MovementInput vector
	FPhysicsCommand::ExecuteWrite(PhysicsActorHandle, [&](const FPhysicsActorHandle& Actor)
	{
		if(!MockPhysics::IsRigidBodyKinematic_AssumesLocked(Actor))
		{
			if(FChaosScene* PhysScene = MockPhysics::GetPhysicsScene(Actor))
			{
				MockPhysics::AddForce_AssumesLocked(Actor, Force, bAllowSubstepping, bAccelChange);
			}
		}
		
	});

	// Apply jump force if bJumpedPressed and not on cooldown
	if (Input.Cmd->bJumpedPressed && Input.Aux->JumpCooldownTime < TimeStep.TotalSimulationTime)
	{
		FPhysicsCommand::ExecuteWrite(PhysicsActorHandle, [&](const FPhysicsActorHandle& Actor)
		{
			if(!MockPhysics::IsRigidBodyKinematic_AssumesLocked(Actor))
			{
				if(FChaosScene* PhysScene = MockPhysics::GetPhysicsScene(Actor))
				{
					FVector JumpForce(0.f, 0.f, 100000.f);
					MockPhysics::AddForce_AssumesLocked(Actor, JumpForce, bAllowSubstepping, bAccelChange);
				}
			}
		});
		
		Output.Aux.Get()->JumpCooldownTime = TimeStep.TotalSimulationTime + 2000; // 2 seccond cooldown
	}
}