// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/NetSerialization.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "WorldCollision.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/OutputDevice.h"
#include "Misc/CoreDelegates.h"

#include "NetworkedSimulationModel.h"
#include "NetworkPredictionComponent.h"
#include "Movement/FlyingMovement.h"

#include "MockAbilitySimulation.generated.h"

// -------------------------------------------------------------------------------------------------------------------------------
// Mock Ability Simulation
//	This is meant to illustrate how a higher level simulation can build off an existing one. While something like GameplayAbilities
//	is more generic and data driven, this illustrates how it will need to solve core issues.
//
//	This implements:
//	1. Stamina "attribute": basic attribute with max + regen value that is consumed by abilities.
//	2. Sprint: Increased max speed while sprint button is held. Drains stamina each frame.
//	3. Dash: Immediate acceleration to a high speed for X seconds.
//	4. Blink: Teleport to location X units ahead
//
//
//	Notes/Todo:
//	-Cooldown/timers for easier tracking (currently relying on high stamina cost to avoid activating each frame)
//	-Outward events for gameplay cue type: tying cosmetic events to everything
//	-More efficient packing of input/pressed buttons. Input handling in general should detect up/down presses rather than just current state
//
// -------------------------------------------------------------------------------------------------------------------------------

// -------------------------------------------------------
// MockAbility Data structures
// -------------------------------------------------------

struct FMockAbilityInputCmd : public FlyingMovement::FInputCmd
{
	bool bSprintPressed = false;
	bool bDashPressed = false;
	bool bBlinkPressed = false;

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << bSprintPressed;
		P.Ar << bDashPressed;
		P.Ar << bBlinkPressed;
		FlyingMovement::FInputCmd::NetSerialize(P);
	}

	void Log(FStandardLoggingParameters& P) const
	{
		FlyingMovement::FInputCmd::Log(P);
		if (P.Context == EStandardLoggingContext::Full)
		{
			P.Ar->Logf(TEXT("bSprintPressed: %d"), bSprintPressed);
			P.Ar->Logf(TEXT("bDashPressed: %d"), bDashPressed);
			P.Ar->Logf(TEXT("bBlinkPressed: %d"), bBlinkPressed);
		}
	}
};

struct FMockAbilitySyncState : public FlyingMovement::FMoveState
{
	float Stamina = 0.f;
	
	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << Stamina;
		FlyingMovement::FMoveState::NetSerialize(P);
	}

	void Log(FStandardLoggingParameters& P) const
	{
		FlyingMovement::FMoveState::Log(P);
		if (P.Context == EStandardLoggingContext::Full)
		{
			P.Ar->Logf(TEXT("Stamina: %.2f"), Stamina);
		}
	}
};

struct FMockAbilityAuxstate : public FlyingMovement::FAuxState
{
	float MaxStamina = 100.f;
	float StaminaRegenRate = 20.f;
	int16 DashTimeLeft = 0;
	int16 BlinkWarmupLeft = 0;
	bool bIsSprinting = false;

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << MaxStamina;
		P.Ar << StaminaRegenRate;
		P.Ar << DashTimeLeft;
		P.Ar << BlinkWarmupLeft;
		P.Ar << bIsSprinting;
		FlyingMovement::FAuxState::NetSerialize(P);
	}

	void Log(FStandardLoggingParameters& P) const
	{
		FlyingMovement::FAuxState::Log(P);
		if (P.Context == EStandardLoggingContext::Full)
		{
			P.Ar->Logf(TEXT("MaxStamina: %.2f"), MaxStamina);
			P.Ar->Logf(TEXT("StaminaRegenRate: %.2f"), StaminaRegenRate);
			P.Ar->Logf(TEXT("DashTimeLeft: %d"), DashTimeLeft);
			P.Ar->Logf(TEXT("BlinkWarmupLeft: %d"), BlinkWarmupLeft);
			P.Ar->Logf(TEXT("bIsSprinting: %d"), bIsSprinting);
		}
	}
};

// -------------------------------------------------------
// MockAbility NetSimCues - events emitted by the sim
// -------------------------------------------------------

// Cue for blink activation.
struct FMockAbilityBlinkCue
{
	NETSIMCUE_BODY();

	FVector_NetQuantize10 StartLocation;
	FVector_NetQuantize10 StopLocation;

	void NetSerialize(FArchive& Ar)
	{
		bool b = false;
		StartLocation.NetSerialize(Ar, nullptr, b);
		StopLocation.NetSerialize(Ar, nullptr, b);
	}

	static bool Unique(const FMockAbilityBlinkCue& A, const FMockAbilityBlinkCue& B)
	{
		const float ErrorTolerance = 1.f;
		return !A.StartLocation.Equals(B.StartLocation, ErrorTolerance) || !A.StopLocation.Equals(B.StopLocation, ErrorTolerance);
	}
};

/*
template<>
struct TCueHandlerTraits<FMockAbilityBlinkCue> : public TNetSimCueTraitsBase//<FMockAbilityBlinkCue>
{
	static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority };
	static constexpr bool Replicate { false };
};
*/

template<>
struct TCueHandlerTraits<FMockAbilityBlinkCue> : public TNetSimCueTraits_ReplicatedNonPredicted { };



// The set of Cues the MockAbility simulation will invoke
struct FMockAbilityCueSet
{
	template<typename TDispatchTable>
	static void RegisterNetSimCueTypes(TDispatchTable& DispatchTable)
	{
		DispatchTable.template RegisterType<FMockAbilityBlinkCue>();
	}
};


// -------------------------------------------------------
// MockAbilitySimulation definition
// -------------------------------------------------------

using TMockAbilityBufferTypes = TNetworkSimBufferTypes<FMockAbilityInputCmd, FMockAbilitySyncState, FMockAbilityAuxstate>;

class FMockAbilitySimulation : public FlyingMovement::FMovementSimulation
{
public:
	/** Tick group the simulation maps to */
	static const FName GroupName;

	/** Main update function */
	void SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<TMockAbilityBufferTypes>& Input, const TNetSimOutput<TMockAbilityBufferTypes>& Output);
};

template<int32 InFixedStepMS=0>
using FMockAbilitySystem = TNetworkedSimulationModel<FMockAbilitySimulation, TMockAbilityBufferTypes, TNetworkSimTickSettings<InFixedStepMS>>;

class IMockFlyingAbilitySystemDriver : public TNetworkedSimulationModelDriver<TMockAbilityBufferTypes> { };

// -------------------------------------------------------------------------------------------------------------------------------
// ActorComponent for running Mock Ability Simulation 
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UMockFlyingAbilityComponent : public UFlyingMovementComponent, public IMockFlyingAbilitySystemDriver
{
	GENERATED_BODY()

public:

	UMockFlyingAbilityComponent();

	DECLARE_DELEGATE_TwoParams(FProduceMockAbilityInput, const FNetworkSimTime /*SimTime*/, FMockAbilityInputCmd& /*Cmd*/)
	FProduceMockAbilityInput ProduceInputDelegate;

	TNetSimStateAccessor<FMockAbilitySyncState> AbilitySyncState;
	TNetSimStateAccessor<FMockAbilityAuxstate> AbilityAuxState;

	// IMockFlyingAbilitySystemDriver
	FString GetDebugName() const override;
	const AActor* GetVLogOwner() const override;
	void VisualLog(const FMockAbilityInputCmd* Input, const FMockAbilitySyncState* Sync, const FMockAbilityAuxstate* Aux, const FVisualLoggingParameters& SystemParameters) const override;

	void ProduceInput(const FNetworkSimTime SimTime, FMockAbilityInputCmd& Cmd) override;
	void FinalizeFrame(const FMockAbilitySyncState& SyncState, const FMockAbilityAuxstate& AuxState) override;

	// Required to supress -Woverloaded-virtual warning. We are effectively hiding UFlyingMovementComponent's methods
	using UFlyingMovementComponent::VisualLog;
	using UFlyingMovementComponent::ProduceInput;
	using UFlyingMovementComponent::FinalizeFrame;

	// NetSimCues
	void HandleCue(FMockAbilityBlinkCue& BlinkCue, const FNetworkSimTime& ElapsedTime);

	// -------------------------------------------------------------------------------------
	//	Ability State and Notifications
	//		-This allows user code/blueprints to respond to state changes.
	//		-These values always reflect the latest simulation state
	//		-StateChange events are just that: when the state changes. They are not emitted from the sim themselves.
	//			-This means they "work" for interpolated simulations and are resilient to packet loss and crazy network conditions
	//			-That said, its "latest" only. There is NO guarantee that you will receive every state transition
	//
	// -------------------------------------------------------------------------------------

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMockAbilityNotifyStateChange, bool, bNewStateValue);

	// Notifies when Sprint state changes
	UPROPERTY(BlueprintAssignable, Category="Mock AbilitySystem")
	FMockAbilityNotifyStateChange OnSprintStateChange;

	// Notifies when Dash state changes
	UPROPERTY(BlueprintAssignable, Category="Mock AbilitySystem")
	FMockAbilityNotifyStateChange OnDashStateChange;

	// Notifies when Blink Changes
	UPROPERTY(BlueprintAssignable, Category="Mock AbilitySystem")
	FMockAbilityNotifyStateChange OnBlinkStateChange;

	// Are we currently in the sprinting state
	UFUNCTION(BlueprintCallable, Category="Mock AbilitySystem")
	bool IsSprinting() const { return bIsSprinting; }

	// Are we currently in the dashing state
	UFUNCTION(BlueprintCallable, Category="Mock AbilitySystem")
	bool IsDashing() const { return bIsDashing; }

	// Are we currently in the blinking (startup) state
	UFUNCTION(BlueprintCallable, Category="Mock AbilitySystem")
	bool IsBlinking() const { return bIsBlinking; }

private:
	
	// Local cached values for detecting state changes from the sim in ::FinalizeFrame
	// Its tempting to think ::FinalizeFrame could pass in the previous frames values but this could
	// not be reliable if buffer sizes are small and network conditions etc - you may not always know
	// what was the "last finalized frame" or even have it in the buffers anymore.
	bool bIsSprinting = false;
	bool bIsDashing = false;
	bool bIsBlinking = false;

protected:

	// Network Prediction
	virtual INetworkedSimulationModel* InstantiateNetworkedSimulation() override;
	FMockAbilitySimulation* MockAbilitySimulation = nullptr;

	template<typename TSimulation>
	void InitMockAbilitySimulation(TSimulation* Simulation, FMockAbilitySyncState& InitialSyncState, FMockAbilityAuxstate& InitialAuxState)
	{
		check(MockAbilitySimulation == nullptr);
		MockAbilitySimulation = Simulation;		

		InitFlyingMovementSimulation(Simulation, InitialSyncState, InitialAuxState);
	}

	template<typename TNetSimModel>
	void InitMockAbilityNetSimModel(TNetSimModel* Model)
	{
		AbilitySyncState.Bind(Model);
		MovementAuxState.Bind(Model);

		InitFlyingMovementNetSimModel(Model);
	}
};