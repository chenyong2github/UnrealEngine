// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Components/ActorComponent.h"
#include "NetworkSimulationModel.h"

#include "NetworkPredictionComponent.generated.h"

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	UNetworkPredictionComponent
//	This is the base component for running a TNetworkedSimulationModel through an actor component. This contains the boiler plate hooks into getting the system
//	initialized and plugged into the UE4 replication system.
//
//	This is an abstract component and cannot function on its own. It must be subclassed and InstantiateNetworkSimulation must be implemented.
//	The subclass is responsible for running the network simulation. This at a bare minimum would mean ticking the simulation and supplying it with input.
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

	// TickComponent calls CheckOwnerRoleChange and invokes PreTickLocallyControlledSim. You should always call it at the top of your child's tick implementation.
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	// PreSimulationTick callback.
	// Called right in ::TickComponent for autonomous proxies. This is optional and intended to be a convencience for generating local input.
	// If you don't use this function, you should manually be submitting client input prior to simulation tick (via GetNextClientInputCmdForWrite).
	// (It is important to submit input prior to ticking the sim. If you get it backwards, you introduce local latency and cause problems with variable time steps).
	DECLARE_DELEGATE_OneParam(FPreSimTickDelegate, float /* DeltaSeconds */)
	void SetLocallyControlledPreTick(const FPreSimTickDelegate& PreSimTick) { PreTickLocallyControlledSim = PreSimTick; }

protected:

	// Child classes must allocate and manage lifetime of their own NetworkSim (E.g, TNetworkedSimulationModel). Recommend to just store in a TUniquePtr.
	// Child classes must also register with Network Sim debugger here (FNetworkSimulationModelDebuggerManager RegisterNetworkSimulationModel)
	virtual IReplicationProxy* InstantiateNetworkSimulation() { return nullptr; }

	// Child classes should override this an initialize their NetworkSim here
	virtual void InitializeForNetworkRole(ENetRole Role) { }

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
};