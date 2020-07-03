// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseMovementComponent.h"
#include "Templates/PimplPtr.h"
#include "MockPhysicsSimulation.h" // For FMockPhysicsInputCmd only for simplified input
#include "MockPhysicsComponent.generated.h"

struct FMockPhysicsInputCmd;
struct FMockPhysicsAuxState;

class FMockPhysicsSimulation;

// -------------------------------------------------------------------------------------------------------------------------------
// ActorComponent for running MockPhysicsSimulation
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UMockPhysicsComponent : public UBaseMovementComponent
{
	GENERATED_BODY()

public:

	UMockPhysicsComponent();

	// Forward input producing event to someone else (probably the owning actor)
	DECLARE_DELEGATE_TwoParams(FProduceMockPhysicsInput, const int32 /*SimTime*/, FMockPhysicsInputCmd& /*Cmd*/)
	FProduceMockPhysicsInput ProduceInputDelegate;

	// Next local InputCmd that will be submitted. This is just one way to do it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=INPUT)
	FMockPhysicsInputCmd PendingInputCmd;

	// --------------------------------------------------------------------------------
	// NP Driver
	// --------------------------------------------------------------------------------

	// Get latest local input prior to simulation step
	void ProduceInput(const int32 DeltaTimeMS, FMockPhysicsInputCmd* Cmd);

	// Take output for simulation
	void FinalizeFrame(const void* SyncState, const FMockPhysicsAuxState* AuxState);

	// Seed initial values based on component's state
	void InitializeSimulationState(const void* Sync, FMockPhysicsAuxState* Aux);

protected:

	// Network Prediction
	virtual void InitializeNetworkPredictionProxy() override;
	TPimplPtr<FMockPhysicsSimulation> OwnedSimulation; // If we instantiate the sim in InitializeNetworkPredictionProxy, its stored here
	FMockPhysicsSimulation* ActiveSimulation = nullptr; // The sim driving us, set in InitFlyingMovementSimulation. Could be child class that implements InitializeNetworkPredictionProxy.

	void InitMockPhysicsSimulation(FMockPhysicsSimulation* Simulation);
};