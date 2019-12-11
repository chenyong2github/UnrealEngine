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

#include "MockNetworkSimulation.generated.h"

class IMockNetworkSimulationDriver;
class IMockDriver;

// -------------------------------------------------------------------------------------------------------------------------------
//	Mock Network Simulation
//
//	This provides a minimal "mock" example of using the Network Prediction system. The simulation being run by these classes
//	is a simple accumulator that takes random numbers (FMockInputCmd::InputValue) as input. There is no movement related functionality
//	in this example. This is just meant to show the bare minimum hook ups into the system to make it easier to understand.
//
//	Highlights:
//		FMockNetworkModel::SimulationTick		The "core update" function of the simulation.
//		UMockNetworkSimulationComponent			The UE4 Actor Component that anchors the system to an actor.
//
//	Usage:
//		You can just add a UMockNetworkSimulationComponent to any ROLE_AutonomousProxy actor yourself (Default Subobject, through blueprints, manually, etc)
//		The console command "mns.Spawn" can be used to dynamically spawn the component on every pawn. Must be run on the server or in single process PIE.
//
//	Once spawned, there are some useful console commands that can be used. These bind to number keys by default (toggleable via mns.BindAutomatically).
//		[Five] "mns.DoLocalInput 1"			can be used to submit random local input into the accumulator. This is how you advance the simulation.
//		[Six]  "mns.RequestMispredict 1"	can be used to force a mispredict (random value added to accumulator server side). Useful for tracing through the correction/resimulate code path.
//		[Nine] "nms.Debug.LocallyControlledPawn"	toggle debug hud for the locally controlled player.
//		[Zero] "nms.Debug.ToggleContinous"			Toggles continuous vs snapshotted display of the debug hud
//
//	Notes:
//		Everything is crammed into MockNetworkSimulation.h/.cpp. It may make sense to break the simulation and component code into
//		separate files for more complex simulations.
//
// -------------------------------------------------------------------------------------------------------------------------------


// -------------------------------------------------------------------------------------------------------------------------------
//	Simulation State
// -------------------------------------------------------------------------------------------------------------------------------

// State the client generates
struct FMockInputCmd
{
	float InputValue=0;

	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << InputValue;
	}

	void Log(FStandardLoggingParameters& P) const
	{
		if (P.Context == EStandardLoggingContext::Full)
		{
			P.Ar->Logf(TEXT("InputValue: %4f"), InputValue);
		}
		else
		{
			P.Ar->Logf(TEXT("%2f"), InputValue);
		}
	}
};

// State we are evolving frame to frame and keeping in sync
struct FMockSyncState
{
	float Total=0;
	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << Total;
	}

	// Compare this state with AuthorityState. return true if a reconcile (correction) should happen
	bool ShouldReconcile(const FMockSyncState& AuthorityState)
	{
		return FMath::Abs<float>(Total - AuthorityState.Total) > SMALL_NUMBER;
	}

	void Log(FStandardLoggingParameters& P) const
	{
		if (P.Context == EStandardLoggingContext::Full)
		{
			P.Ar->Logf(TEXT("Total: %4f"), Total);
		}
		else
		{
			P.Ar->Logf(TEXT("%2f"), Total);
		}
	}

	static void Interpolate(const FMockSyncState& From, const FMockSyncState& To, const float PCT, FMockSyncState& OutDest)
	{
		OutDest.Total = From.Total + ((To.Total - From.Total) * PCT);
	}
};

// Auxiliary state that is input into the simulation. Doesn't change during the simulation tick.
// (It can change and even be predicted but doing so will trigger more bookeeping, etc. Changes will happen "next tick")
struct FMockAuxState
{
	float Multiplier=1;
	void NetSerialize(const FNetSerializeParams& P)
	{
		P.Ar << Multiplier;
	}

	void Log(FStandardLoggingParameters& Params) const
	{
		if (Params.Context == EStandardLoggingContext::HeaderOnly)
		{
			Params.Ar->Logf(TEXT(" %d "), Params.Frame);
		}
		else if (Params.Context == EStandardLoggingContext::Full)
		{
			Params.Ar->Logf(TEXT("Multiplier: %f"), Multiplier);
		}
	}
};

// -------------------------------------------------------------------------------------------------------------------------------
//	Simulation Cues
// -------------------------------------------------------------------------------------------------------------------------------

// A minimal Cue, emitted every 10 "totals" with a random integer as a payload
struct FMockCue
{
	FMockCue() = default;
	FMockCue(int32 InRand)
		: RandomData(InRand) { }

	NETSIMCUE_BODY();
	int32 RandomData = 0;
};

// Set of all cues the Mock simulation emits. Not strictly necessary and not that helpful when there is only one cue (but using since this is the example real simulations will want to use)
struct FMockCueSet
{
	template<typename TDispatchTable>
	static void RegisterNetSimCueTypes(TDispatchTable& DispatchTable)
	{
		DispatchTable.template RegisterType<FMockCue>();
	}
};

// -------------------------------------------------------------------------------------------------------------------------------
//	The Simulation
// -------------------------------------------------------------------------------------------------------------------------------

using TMockNetworkSimulationBufferTypes = TNetworkSimBufferTypes<FMockInputCmd, FMockSyncState, FMockAuxState>;

class FMockNetworkSimulation
{
public:

	/** Main update function */
	void SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<TMockNetworkSimulationBufferTypes>& Input, const TNetSimOutput<TMockNetworkSimulationBufferTypes>& Output);
};

// -------------------------------------------------------------------------------------------------------------------------------
//	Networked Simulation Model Def
// -------------------------------------------------------------------------------------------------------------------------------

class FMockNetworkModelDef : public FNetSimModelDefBase
{
public:

	using Simulation = FMockNetworkSimulation;
	using BufferTypes = TMockNetworkSimulationBufferTypes;

	/** Tick group the simulation maps to */
	static const FName GroupName;

	/** Predicted error testing */
	static bool ShouldReconcile(const FMockSyncState& AuthoritySync, const FMockAuxState& AuthorityAux, const FMockSyncState& PredictedSync, const FMockAuxState& PredictedAux)
	{
		return FMath::Abs<float>(AuthoritySync.Total - PredictedSync.Total) > SMALL_NUMBER;
	}

	static void Interpolate(const TInterpolatorParameters<FMockSyncState, FMockAuxState>& Params)
	{
		Params.Out.Sync.Total = Params.From.Sync.Total + ((Params.To.Sync.Total - Params.From.Sync.Total) * Params.InterpolationPCT);
		Params.Out.Aux = Params.To.Aux;
	}
};

// -------------------------------------------------------------------------------------------------------------------------------
// ActorComponent for running a MockNetworkSimulation (implements Driver for the mock simulation)
// -------------------------------------------------------------------------------------------------------------------------------

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTIONEXTRAS_API UMockNetworkSimulationComponent : public UNetworkPredictionComponent, public TNetworkedSimulationModelDriver<TMockNetworkSimulationBufferTypes>
{
	GENERATED_BODY()

public:

	UMockNetworkSimulationComponent();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	
	virtual INetworkedSimulationModel* InstantiateNetworkedSimulation() override;
	
public:

	FString GetDebugName() const override;
	AActor* GetVLogOwner() const override;
	void VisualLog(const FMockInputCmd* Input, const FMockSyncState* Sync, const FMockAuxState* Aux, const FVisualLoggingParameters& SystemParameters) const override;
	
	void ProduceInput(const FNetworkSimTime SimTime, FMockInputCmd& Cmd);
	void FinalizeFrame(const FMockSyncState& SyncState, const FMockAuxState& AuxState) override;

	void HandleCue(const FMockCue& MockCue, const FNetSimCueSystemParamemters& SystemParameters);

	// Mock representation of "syncing' to the sync state in the network sim.
	float MockValue = 1000.f;

	FMockNetworkSimulation* MockNetworkSimulation = nullptr;
};