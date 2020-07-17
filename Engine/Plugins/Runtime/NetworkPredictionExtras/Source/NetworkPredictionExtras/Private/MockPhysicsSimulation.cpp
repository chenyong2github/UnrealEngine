// Copyright Epic Games, Inc. All Rights Reserved.

#include "MockPhysicsSimulation.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"
#include "PBDRigidsSolver.h"
#include "Chaos/ChaosScene.h"
#include "Chaos/EvolutionTraits.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Physics/GenericPhysicsInterface.h"
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"
#include "PhysicsEngine/BodyInstance.h"

NETSIMCUE_REGISTER(FMockPhysicsJumpCue, TEXT("MockPhysicsJumpCue"));
NETSIMCUE_REGISTER(FMockPhysicsChargeCue, TEXT("FMockPhysicsChargeCue"));

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

	bool GeomOverlapMulti(UWorld* World, const FCollisionShape& InGeom, const FVector& InPosition, const FQuat& InRotation, TArray<FOverlapResult>& OutOverlaps, ECollisionChannel TraceChannel, const FCollisionQueryParams& Params, const FCollisionResponseParams& ResponseParams, const FCollisionObjectQueryParams& ObjectParams)
	{
		return FGenericPhysicsInterface::GeomOverlapMulti(World, InGeom, InPosition, InRotation, OutOverlaps, TraceChannel, Params, ResponseParams, ObjectParams);
		
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
	Force.Z = 0.f;

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
					FVector JumpForce(0.f, 0.f, 50000.f);
					MockPhysics::AddForce_AssumesLocked(Actor, JumpForce, bAllowSubstepping, bAccelChange);
				}
			}
		});
		
		Output.Aux.Get()->JumpCooldownTime = TimeStep.TotalSimulationTime + 2000; // 2 seccond cooldown

		// Jump cue: this will emit an event to the game code to play a particle or whatever they want
		Output.CueDispatch.Invoke<FMockPhysicsJumpCue>( PhysicsActorHandle->X() );
	}


	// Charge: do radial impulse when Charge input is released + some delay
	if (Input.Aux->ChargeEndTime != 0)
	{
		const int32 TimeSinceChargeEnd = TimeStep.TotalSimulationTime - Input.Aux->ChargeEndTime;
		if (TimeSinceChargeEnd > 100)
		{
			Output.Aux.Get()->ChargeEndTime = 0;
			

			UWorld* World = FPhysicsInterface::GetCurrentScene(PhysicsActorHandle)->GetSolver()->GetOwner()->GetWorld();	// Awkward isn't really actually needed (only used by GeomOverlapMultiImp to get physics scene and debug drawing)
			FCollisionShape Shape = FCollisionShape::MakeSphere(250.f);
			FVector TracePosition = PhysicsActorHandle->X();

			FQuat Rotation = FQuat::Identity;
			ECollisionChannel CollisionChannel = ECollisionChannel::ECC_PhysicsBody; // obviously not good to hardcode, could be some property accessed via this simulation object
			FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
			FCollisionResponseParams ResponseParams = FCollisionResponseParams::DefaultResponseParam;
			FCollisionObjectQueryParams ObjectParams(ECollisionChannel::ECC_PhysicsBody);

			TArray<FOverlapResult> Overlaps;

			MockPhysics::GeomOverlapMulti(World, Shape, TracePosition, Rotation, Overlaps, CollisionChannel, QueryParams, ResponseParams, ObjectParams);

			for (FOverlapResult& Result : Overlaps)
			{
				//UE_LOG(LogTemp, Warning, TEXT("  Hit: %s"), *GetNameSafe(Result.Actor.Get()));
				if (UPrimitiveComponent* PrimitiveComp = Result.Component.Get())
				{
					// We can't use the Primitive component's data here because it is not up to date!
					// We need to get the real physics data from the underlying body.
					FBodyInstance* Instance = PrimitiveComp->GetBodyInstance();
					if (Instance)
					{
						FPhysicsActorHandle HitHandle = Instance->GetPhysicsActorHandle();
						FVector PhysicsLocation = HitHandle->X();

						FVector Dir = (PhysicsLocation - TracePosition);
						Dir.Z = 0.f;
						Dir.Normalize();

						FVector Impulse = Dir * 100000.f;
						Impulse.Z = 100000.f;

						PrimitiveComp->AddImpulseAtLocation(Impulse, TracePosition);
					}
				}
			}
			Output.CueDispatch.Invoke<FMockPhysicsChargeCue>( TracePosition );
		}
	}
	else
	{
		const bool bWasCharging = (Input.Aux->ChargeStartTime != 0);

		if (!bWasCharging && Input.Cmd->bChargePressed)
		{
			// Press
			Output.Aux.Get()->ChargeStartTime = TimeStep.TotalSimulationTime;
			Output.Aux.Get()->ChargeEndTime = 0;
		}

		if (bWasCharging && !Input.Cmd->bChargePressed)
		{
			Output.Aux.Get()->ChargeStartTime = 0;
			Output.Aux.Get()->ChargeEndTime = TimeStep.TotalSimulationTime;
		}
	}
}