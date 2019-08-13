// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Components/ActorComponent.h"
#include "NetworkPredictionTypes.h"

#include "NetworkPredictionComponent.generated.h"

class INetworkSimulationOwner
{
	virtual void Reconcile() = 0;
	virtual void TickSimulation(float DeltaTimeSeconds) = 0;
};

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	UNetworkPredictionComponent
//	This is the base component for running a TNetworkedSimulationModel through an actor component. This contains the boiler plate hooks into getting the system
//	initialized and plugged into the UE4 replication system.
//
//	This is an abstract component and cannot function on its own. It must be subclassed and InstantiateNetworkSimulation must be implemented.
//	The subclass is responsible for running the network simulation. This at a bare minimum would mean ticking the simulation and supplying it with input.
//
//	Its also worth pointing out that nothing about being a UActorComponent is essential here. All that this component does could be done within an AActor itself.
//	An actor component makes sense for flexible/reusable code provided by the plugin. But there is nothing stopping you from copying this directly into an actor if you had reason to.
// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
UCLASS(Abstract)
class NETWORKPREDICTION_API UNetworkPredictionComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UNetworkPredictionComponent();

	virtual void InitializeComponent() override;
	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	virtual void PreNetReceive() override;

	// PreSimulationTick callback.
	// Called right in ::TickComponent for autonomous proxies. This is optional and intended to be a convencience for generating local input.
	// If you don't use this function, you should manually be submitting client input prior to simulation tick (via GetNextClientInputCmdForWrite).
	// (It is important to submit input prior to ticking the sim. If you get it backwards, you introduce local latency and cause problems with variable time steps).
	DECLARE_DELEGATE_OneParam(FPreSimTickDelegate, float /* DeltaSeconds */)
	void SetLocallyControlledPreTick(const FPreSimTickDelegate& PreSimTick) { PreTickLocallyControlledSim = PreSimTick; }
	
	virtual void Reconcile() { }
	virtual void TickSimulation(float DeltaTimeSeconds) { }

	void PreTickSimulation(float DeltaTime);

protected:

	// Child classes must allocate and manage lifetime of their own NetworkSim (E.g, TNetworkedSimulationModel). Recommend to just store in a TUniquePtr.
	// Child classes must also register with Network Sim debugger here (FNetworkSimulationModelDebuggerManager RegisterNetworkSimulationModel)
	virtual IReplicationProxy* InstantiateNetworkSimulation() { check(false); return nullptr; }

	// Child must override this an initialize their NetworkSim here (call 
	virtual void InitializeForNetworkRole(ENetRole Role) { check(false); }

	// Doesn't need to be overridden. Expected to be used in ::InitializeForNetworkRole implementation
	virtual FNetworkSimulationModelInitParameters GetSimulationInitParameters(ENetRole Role);

	// Attempts to determine if the component is locally controlled or not, meaning we expect input cmds to be generated locally (and hence need to prepare the input buffer for that)
	virtual bool IsLocallyControlled();

	// Helper: Checks if the owner's role has changed and calls InitializeForNetworkRole if necessary.
	// NOTE: You may need to call this in your TickComponent function
	void CheckOwnerRoleChange();
	ENetRole OwnerCachedNetRole = ROLE_None;

	UFUNCTION(Server, unreliable, WithValidation)
	void ServerRecieveClientInput(const FServerReplicationRPCParameter& ProxyParameter);

	// ReplicationProxies are just pointers to the data/NetSerialize functions within the NetworkSim
	UPROPERTY()
	FReplicationProxy ReplicationProxy_ServerRPC;

private:

	FPreSimTickDelegate PreTickLocallyControlledSim;

	UPROPERTY(Replicated, transient)
	FReplicationProxy ReplicationProxy_Autonomous;

	UPROPERTY(Replicated, transient)
	FReplicationProxy ReplicationProxy_Simulated;

	UPROPERTY(Replicated, transient)
	FReplicationProxy ReplicationProxy_Replay;

	UPROPERTY(Replicated, transient)
	FReplicationProxy ReplicationProxy_Debug;

protected:

	UFUNCTION()
	virtual void OnRep_SimulatedProxy() { }
};