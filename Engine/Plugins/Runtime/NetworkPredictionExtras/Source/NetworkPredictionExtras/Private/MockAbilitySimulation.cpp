// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MockAbilitySimulation.h"
#include "DrawDebugHelpers.h"
#include "VisualLogger/VisualLogger.h"

namespace MockAbilityCVars
{
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(DefaultMaxSpeed, 1200.f, "mockability.DefaultMaxSpeed", "Default Speed");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(DefaultAcceleration, 4000.f, "mockability.DefaultAcceleration", "Default Acceleration");

	NETSIM_DEVCVAR_SHIPCONST_FLOAT(SprintMaxSpeed, 5000.f, "mockability.SprintMaxSpeed", "Max Speed when sprint is applied.");

	NETSIM_DEVCVAR_SHIPCONST_FLOAT(DashMaxSpeed, 7500.f, "mockability.DashMaxSpeed", "Max Speed when dashing.");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(DashAcceleration, 100000.f, "mockability.DashAcceleration", "Acceleration when dashing.");
}

// -------------------------------------------------------------------------------------------------------------
//
// -------------------------------------------------------------------------------------------------------------

const FName FMockAbilitySimulation::GroupName(TEXT("Ability"));

void FMockAbilitySimulation::SimulationTick(const TNetSimTimeStep& TimeStep, const TNetSimInput<TMockAbilityBufferTypes>& Input, const TNetSimOutput<TMockAbilityBufferTypes>& Output)
{
	// WIP NOTES:
	//	-We are creating local copies of the input state that we can mutate and pass on to the parent simulation's tick (LocalCmd, LocalSync, LocalAux). This may seem weird but consider:
	//		-Modifying the actual inputs is a bad idea: inputs are "final" once TNetworkedSimulationModel calls SimulationTick. The sim itself should not modify the inputs after this.
	//		-Tempting to just use the already-allocated Output states and pass them to the parent sim as both Input/Output. But now &Input.Sync == &Output.Sync for example! The parent sim
	//			may write to Output and then later check it against the passed in input (think "did this change"), having no idea that by writing to output he was also writing to input! This seems like it should be avoided.
	//		-It may be worth considering "lazy copying" or allocating of these temp states... but for now that adds more complexity and isn't necessary since the local copies are stack allocated and
	//			these state structures are small and contiguous.
	//
	//	-Still, there are some awkwardness when we need to write to both the local temp state *and* the output state explicitly.
	//	-Stamina changes for example: we want to both change the stamina both for this frame (used by rest of code in this function) AND write it to the output state (so it comes in as input next frame)
	//		-Since this sim "owns" stamina, we could copy it immediately to the output state and use the output state exclusively in this code...
	//		-... but that is inconsistent with state that we don't "own" like Location. Since the parent sim will generate a new Location and we can't pass output to the parent as input
	//			-You would be in a situation where "Teleport modified LocalSync.Location" and "another place modified Output.Sync.Stamina".
	//	
	//
	//	Consider: not using inheritance for simulations. Would that make this less akward? 
	//		-May make Sync state case above less weird since it would breakup stamina/location ("owned" vs "not owned"). Stamina could always use output state and we'd have a local copy of the movement input sync state.
	//		-Aux state is still bad even without inheritance: if you want to read and write to an aux variable at different points in the frames, hard to know where "real" value is at any given time without
	//			writing fragile code. Making code be explicit: "I am writing to the current value this frame AND next frames value" seems ok. Could possibly do everything in a local copy and then see if its changed
	//			at the end up ::SimulationTick and then copy it into output aux. Probably should avoid comparison operator each frame so would need to wrap in some struct that could track a dirty flag... seems complicating.
	//

	check(EventHandler);
	const float DeltaSeconds = TimeStep.StepMS.ToRealTimeSeconds();

	// Local copies of the const input that we will pass into the parent sim as input.
	FMockAbilityInputCmd LocalCmd = Input.Cmd;
	FMockAbilitySyncState LocalSync = Input.Sync;
	FMockAbilityAuxstate LocalAux = Input.Aux;

	const bool bAlreadyBlinking = Input.Aux.BlinkWarmupLeft > 0;
	const bool bAlreadyDashing = Input.Aux.DashTimeLeft > 0;
	const bool bAllowNewActivations = !bAlreadyBlinking && !bAlreadyDashing;

	// -------------------------------------------------------------------------
	//	Regen
	//	-Applies at the start of frame: you can spend it immediately
	//	-hence we have to write to both the LocalSync's stamina (what the rest of the abilities look at for input) and output (what value will be used as input for next frame)
	// -------------------------------------------------------------------------

	{
		const float NewStamina = FMath::Min<float>( LocalSync.Stamina + (DeltaSeconds * LocalAux.StaminaRegenRate), LocalAux.MaxStamina );
		LocalSync.Stamina = NewStamina;
		Output.Sync.Stamina = NewStamina;
	}

	// -------------------------------------------------------------------------
	//	Blink
	// -------------------------------------------------------------------------

	static float BlinkCost = 75.f;
	static int16 BlinkWarmupMS = 750; 

	const bool bBlinkActivate = (Input.Cmd.bBlinkPressed && Input.Sync.Stamina > BlinkCost && bAllowNewActivations);
	if (bBlinkActivate)
	{
		LocalAux.BlinkWarmupLeft = BlinkWarmupMS;
		EventHandler->NotifyBlinkStartup();
	}

	if (LocalAux.BlinkWarmupLeft > 0)
	{
		const int16 NewBlinkWarmupLeft = FMath::Max<int16>(0, LocalAux.BlinkWarmupLeft - TimeStep.StepMS);
		Output.Aux.Get()->BlinkWarmupLeft = NewBlinkWarmupLeft;

		// While blinking is warming up, we disable movement input and the other actions
		LocalCmd.MovementInput.Set(0.f, 0.f, 0.f);
		LocalCmd.bDashPressed = false;
		LocalCmd.bSprintPressed = false;

		LocalSync.Velocity.Set(0.f, 0.f, 0.f);

		if (NewBlinkWarmupLeft <= 0)
		{
			static float BlinkDist = 1000.f;
			FVector DestLocation = LocalSync.Location + LocalSync.Rotation.RotateVector( FVector(BlinkDist, 0.f, 0.f) );
			AActor* OwningActor = UpdatedComponent->GetOwner();
			check(OwningActor);

			// DrawDebugLine(OwningActor->GetWorld(), Input.Sync.Location, DestLocation, FColor::Red, false);	

			// Its unfortunate teleporting is so complicated. It may make sense for a new movement simulation to define this themselves, 
			// but for this mock one, we will just use the engine's AActor teleport.
			if (OwningActor->TeleportTo(DestLocation, LocalSync.Rotation))
			{
				// Component now has the final location
				const FTransform UpdateComponentTransform = GetUpdateComponentTransform();
				LocalSync.Location = UpdateComponentTransform.GetLocation();
				
				const float NewStamina = FMath::Max<float>(0.f, LocalSync.Stamina - BlinkCost);
				LocalSync.Stamina = NewStamina;
				Output.Sync.Stamina = NewStamina;

				EventHandler->NotifyBlinkFinished();
			}
		}
	}

	// -------------------------------------------------------------------------
	//	Dash
	//	-Stamina consumed on initial press
	//	-MaxSpeed/Acceleration are jacked up
	//	-Dash lasts for 400ms (DashDurationMS)
	//		-Division of frame times can cause you to dash for longer. We would have to break up simulation steps to support this 100% accurately.
	//	-Movement input is synthesized while in dash state. That is, we force forward movement and ignore what was actually fed into the simulation (move input only)
	// -------------------------------------------------------------------------

	static float DashCost = 75.f;
	static int16 DashDurationMS = 500;
	
	const bool bDashActivate = (Input.Cmd.bDashPressed && Input.Sync.Stamina > DashCost && bAllowNewActivations);
	if (bDashActivate)
	{
		// Start dashing
		LocalAux.DashTimeLeft = DashDurationMS;
		
		const float NewStamina = FMath::Max<float>(0.f, LocalSync.Stamina - DashCost);
		LocalSync.Stamina = NewStamina;
		Output.Sync.Stamina = NewStamina;

		EventHandler->NotifyDash(true);
	}

	if (LocalAux.DashTimeLeft > 0)
	{
		const int16 NewDashTimeLeft = FMath::Max<int16>(LocalAux.DashTimeLeft - (int16)(DeltaSeconds * 1000.f), 0);
		Output.Aux.Get()->DashTimeLeft = NewDashTimeLeft;

		LocalAux.MaxSpeed = MockAbilityCVars::DashMaxSpeed();
		LocalAux.Acceleration = MockAbilityCVars::DashAcceleration();

		LocalCmd.MovementInput.Set(1.f, 0.f, 0.f);
		LocalCmd.bBlinkPressed = false;
		LocalCmd.bSprintPressed = false;
		
		FlyingMovement::FMovementSimulation::SimulationTick(TimeStep, { LocalCmd, LocalSync, LocalAux }, { Output.Sync, Output.Aux });

		if (NewDashTimeLeft == 0)
		{
			// Stop when dash is over
			Output.Sync.Velocity.Set(0.f, 0.f, 0.f);
			EventHandler->NotifyDash(false);
		}

		return;
	}


	// -------------------------------------------------------------------------
	//	Sprint
	//	-Requires 15% stamina to start activation
	//	-Drains at 100 stamina/second after that
	//	-Just bumps up max move speed
	//	-Note how this is a transient value: it is fed into the parent sim's input but never makes it to an output state
	// -------------------------------------------------------------------------
	
	static float SprintBaseCost = 100.f; // 100 sprint/second cost
	static float SprintStartMinPCT = 0.15f; // 15% stamina required to begin sprinting
	
	bool bIsSprinting = false;
	if (Input.Cmd.bSprintPressed && bAllowNewActivations)
	{
		const float SprintCostThisFrame = SprintBaseCost * DeltaSeconds;

		if (Input.Aux.bIsSprinting)
		{
			// If we are already sprinting, then we keep sprinting as long as we can afford the absolute cost
			bIsSprinting = LocalSync.Stamina > SprintCostThisFrame;
		}
		else
		{
			// To start sprinting, we have to have SprintStartMinPCT stamina
			bIsSprinting = LocalSync.Stamina > (SprintStartMinPCT * LocalAux.MaxStamina);
		}

		if (bIsSprinting)
		{
			LocalAux.MaxSpeed = MockAbilityCVars::SprintMaxSpeed();

			const float NewStamina = FMath::Max<float>(0.f, LocalSync.Stamina - SprintCostThisFrame);
			LocalSync.Stamina = NewStamina;
			Output.Sync.Stamina = NewStamina;

		}
	}

	// Update the out aux state and call notifies only if sprinting state as actually changed
	if (bIsSprinting != Input.Aux.bIsSprinting)
	{
		EventHandler->NotifySprint(bIsSprinting);
		Output.Aux.Get()->bIsSprinting = bIsSprinting;
	}

	FlyingMovement::FMovementSimulation::SimulationTick(TimeStep, { LocalCmd, LocalSync, LocalAux }, { Output.Sync, Output.Aux });
}

// -------------------------------------------------------------------------------------------------------------
//
// -------------------------------------------------------------------------------------------------------------

UMockFlyingAbilityComponent::UMockFlyingAbilityComponent()
{

}

INetworkSimulationModel* UMockFlyingAbilityComponent::InstantiateNetworkSimulation()
{
	check(UpdatedComponent);
	
	// FIXME: init sim helper on super?
	MockAbilitySimulation.Reset(new FMockAbilitySimulation());
	MockAbilitySimulation->UpdatedComponent = UpdatedComponent;
	MockAbilitySimulation->UpdatedPrimitive = UpdatedPrimitive;
	MockAbilitySimulation->EventHandler = this;
	
	FMockAbilitySyncState InitialSyncState;
	InitialSyncState.Location = UpdatedComponent->GetComponentLocation();
	InitialSyncState.Rotation = UpdatedComponent->GetComponentQuat().Rotator();	

	FMockAbilityAuxstate InitialAuxState;
	InitialAuxState.MaxSpeed = MockAbilityCVars::DefaultMaxSpeed();

	auto NewModel = new FMockAbilitySystem<0>(MockAbilitySimulation.Get(), this, InitialSyncState, InitialAuxState);

	MovementSyncState.Init(NewModel);
	MovementAuxState.Init(NewModel);
	return NewModel;
}

void UMockFlyingAbilityComponent::ProduceInput(const FNetworkSimTime SimTime, FMockAbilityInputCmd& Cmd)
{
	// This isn't ideal. It probably makes sense for the component to do all the input binding rather.
	ProduceInputDelegate.ExecuteIfBound(SimTime, Cmd);
}

void UMockFlyingAbilityComponent::FinalizeFrame(const FMockAbilitySyncState& SyncState, const FMockAbilityAuxstate& AuxState)
{
	Super::FinalizeFrame(SyncState, AuxState);

	if (AuxState.bIsSprinting != bIsSprinting)
	{
		bIsSprinting = AuxState.bIsSprinting;
		OnSprintStateChange.Broadcast(AuxState.bIsSprinting);
	}

	const bool bLocalIsDashing = (AuxState.DashTimeLeft > 0);
	if (bLocalIsDashing != bIsDashing)
	{
		bIsDashing = bLocalIsDashing;
		OnDashStateChange.Broadcast(bIsDashing);
	}

	const bool bLocalIsBlinking = (AuxState.BlinkWarmupLeft > 0);
	if (bLocalIsBlinking != bIsBlinking)
	{
		bIsBlinking = bLocalIsBlinking;
		OnBlinkStateChange.Broadcast(bLocalIsBlinking);
	}
}

FString UMockFlyingAbilityComponent::GetDebugName() const
{
	return FString::Printf(TEXT("MockAbility. %s. %s"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwnerRole()), *GetName());
}

const AActor* UMockFlyingAbilityComponent::GetVLogOwner() const
{
	return GetOwner();
}

void UMockFlyingAbilityComponent::VisualLog(const FMockAbilityInputCmd* Input, const FMockAbilitySyncState* Sync, const FMockAbilityAuxstate* Aux, const FVisualLoggingParameters& SystemParameters) const
{
	Super::VisualLog(Input, Sync, Aux, SystemParameters);
}

// ---------------------------------------------------------------------------------

void UMockFlyingAbilityComponent::NotifySprint(bool bNewIsSprinting)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	//UE_LOG(LogTemp, Display, TEXT("Sprint: %d"), bNewIsSprinting);
}

void UMockFlyingAbilityComponent::NotifyDash(bool bNewIsDashing)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	//UE_LOG(LogTemp, Display, TEXT("Dash: %d"), bNewIsDashing);
}

void UMockFlyingAbilityComponent::NotifyBlinkStartup()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	//UE_LOG(LogTemp, Display, TEXT("Blink Startup"));
}

void UMockFlyingAbilityComponent::NotifyBlinkFinished()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	//UE_LOG(LogTemp, Display, TEXT("Blink Finished"));
}
