// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "NetworkSimulationModelDebugger.h"

#include "NetworkSimulationGlobalManager.generated.h"

// ----------------------------------------------------------------------------------------------------------------------------
//	The purpose of the global manager is to enforce frame boundaries and ordering amongst all instances of network simulations.
//	Other than registration it is not something much code should interact with. It mainly manages when network sims reconcile and tick.
//
//
//	Frame Overview:
//
//		Network TickFlush (receive network traffic)
//			NetSerialize is called on individual net sims (in an essentially random order)
//				RepNotifies, PostNetReceive, etc are called on actors *right after they individually receive traffic* (this is kind of unfortunate but how it is)
//		
//		UNetworkSimulationGlobalManager::ReconcilePostNetworkUpdate	 (Reconcile is called in deterministic order on all network sims)
//			(think: "Get yourself right with the latest update from the server *before* we start a new frame")
//
//		UNetworkSimulationGlobalManager::BeginNewSimulationFrame ("Add" "allowed" time into the global "space" which will allow sims to continue to tick)
//			Simulations are ticked in deterministic order (if registered with UNetworkSimulationGlobalManager)
//
//		UE4 Actors Tick
//
//		Network Tick Dispatch (send outgoing network traffic)
//
//
// ----------------------------------------------------------------------------------------------------------------------------

class IReplicationProxy;

UCLASS()
class UNetworkSimulationGlobalManager : public UWorldSubsystem
{
	GENERATED_BODY()
public:

	UNetworkSimulationGlobalManager();
	
	// Subsystem Init/Deinit
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Netsim Register/Unregister
	void RegisterModel(INetworkSimulationModel* Model, AActor* OwningActor);
	void UnregisterModel(INetworkSimulationModel* Model, AActor* OwningActor);

	// Delegate for auto proxies to register with to send their server RPC
	DECLARE_MULTICAST_DELEGATE_OneParam(FTickServerRPC, float /*RealTime DeltaSeconds*/)
	FTickServerRPC TickServerRPCDelegate;

private:

	void ReconcileSimulationsPostNetworkUpdate();
	void BeginNewSimulationFrame(UWorld* InWorld, ELevelTick InLevelTick, float InDeltaSeconds);
	
	// Structure for organizing our simulations into groups to give some ticking order.
	// This will need to evolve over time a bit and be optimized for larger number of simulations.
	struct FSimulationGroup
	{
		struct FItem
		{
			FItem(INetworkSimulationModel* InSim, AActor* InActor) : Simulation(InSim), OwningActor(InActor) { }
			bool operator == (const INetworkSimulationModel* InSim) const { return Simulation == InSim; }

			INetworkSimulationModel* Simulation;
			AActor* OwningActor;
		};

		TArray<FItem> Simulations;
	};

	TSortedMap<FName, FSimulationGroup, FDefaultAllocator, FNameFastLess> SimulationGroupMap;

	FDelegateHandle PostTickDispatchHandle;
	FDelegateHandle PreWorldActorTickHandle;
};