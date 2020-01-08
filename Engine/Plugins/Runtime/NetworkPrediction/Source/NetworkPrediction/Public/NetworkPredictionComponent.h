// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Components/ActorComponent.h"
#include "NetworkPredictionTypes.h"

#include "NetworkPredictionComponent.generated.h"

// ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
//	UNetworkPredictionComponent
//	This is the base component for running a TNetworkedSimulationModel through an actor component. This contains the boiler plate hooks into getting the system
//	initialized and plugged into the UE4 replication system.
//
//	This is an abstract component and cannot function on its own. It must be subclassed and InstantiateNetworkedSimulation must be implemented.
//	Ticking and RPC sending will be handled automatically, but the subclass will need to supply input to the simulation (via TNetworkedSimulationModelDriver::ProduceInput).
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
	virtual void UninitializeComponent() override;
	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	virtual void PreNetReceive() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	
	virtual void Reconcile() { }
	virtual void TickSimulation(float DeltaTimeSeconds) { }

	INetworkedSimulationModel* GetNetworkSimulation() const { return NetSimModel.Get(); }

protected:
	
	// Classes must instantiate their own NetworkSim here. The UNetworkPredictionComponent will manage its lifetime
	virtual INetworkedSimulationModel* InstantiateNetworkedSimulation() { check(false); return nullptr; }

	// Finalizes initialization when NetworkRole changes. Does not need to be overridden.
	virtual void InitializeForNetworkRole(ENetRole Role);

	// Doesn't need to be overridden. Expected to be used in ::InitializeForNetworkRole implementation
	virtual FNetworkSimulationModelInitParameters GetSimulationInitParameters(ENetRole Role);

	// Attempts to determine if the component is locally controlled or not, meaning we expect input cmds to be generated locally (and hence need to prepare the input buffer for that)
	virtual bool IsLocallyControlled();

	// Helper: Checks if the owner's role has changed and calls InitializeForNetworkRole if necessary.
	// NOTE: You may need to call this in your TickComponent function
	bool CheckOwnerRoleChange();
	ENetRole OwnerCachedNetRole = ROLE_None;

	// The actual ServerRPC. This needs to be a UFUNCTION but is generic and not dependent on the simulation instance
	UFUNCTION(Server, unreliable, WithValidation)
	void ServerRecieveClientInput(const FServerReplicationRPCParameter& ProxyParameter);

	// ReplicationProxies are just pointers to the data/NetSerialize functions within the NetworkSim
	UPROPERTY()
	FReplicationProxy ReplicationProxy_ServerRPC;

private:

	UPROPERTY(Replicated, transient)
	FReplicationProxy ReplicationProxy_Autonomous;

	UPROPERTY(Replicated, transient)
	FReplicationProxy ReplicationProxy_Simulated;

	UPROPERTY(Replicated, transient)
	FReplicationProxy ReplicationProxy_Replay;

	UPROPERTY(Replicated, transient)
	FReplicationProxy ReplicationProxy_Debug;

protected:

	// Called via ServerRPCDelegate, ticks serverRPC timing and decides whether to send the RPC or not
	void TickServerRPC(float DeltaSeconds);

	void RegisterServerRPCDelegate();
	void UnregisterServerRPCDelegate();
	FDelegateHandle ServerRPCHandle;

	// The Network sim that this component is managing. This is what is doing all the work.
	TUniquePtr<INetworkedSimulationModel> NetSimModel;
};