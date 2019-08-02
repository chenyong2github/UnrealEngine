// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "NetworkPredictionComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

UNetworkPredictionComponent::UNetworkPredictionComponent()
{
	bReplicates = true;
}

void UNetworkPredictionComponent::InitializeComponent()
{
	Super::InitializeComponent();

	IReplicationProxy* NetworkSim = InstantiateNetworkSimulation();
	check(NetworkSim);

	ReplicationProxy_ServerRPC.Init(NetworkSim, EReplicationProxyTarget::ServerRPC);
	ReplicationProxy_Autonomous.Init(NetworkSim, EReplicationProxyTarget::AutonomousProxy);
	ReplicationProxy_Simulated.Init(NetworkSim, EReplicationProxyTarget::SimulatedProxy);
	ReplicationProxy_Replay.Init(NetworkSim, EReplicationProxyTarget::Replay);
#if NETSIM_MODEL_DEBUG
	ReplicationProxy_Debug.Init(NetworkSim, EReplicationProxyTarget::Debug);
#endif
}

void UNetworkPredictionComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// We have to update our replication proxies so they can be accurately compared against client shadowstate during property replication. ServerRPC proxy does not need to do this.
	ReplicationProxy_Autonomous.OnPreReplication();
	ReplicationProxy_Simulated.OnPreReplication();
	ReplicationProxy_Replay.OnPreReplication();
#if NETSIM_MODEL_DEBUG
	ReplicationProxy_Debug.OnPreReplication();
#endif
}

void UNetworkPredictionComponent::PreNetReceive()
{
	Super::PreNetReceive();
	CheckOwnerRoleChange();
}

void UNetworkPredictionComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION( UNetworkPredictionComponent, ReplicationProxy_Autonomous, COND_AutonomousOnly);
	DOREPLIFETIME_CONDITION( UNetworkPredictionComponent, ReplicationProxy_Simulated, COND_SimulatedOnly);
	DOREPLIFETIME_CONDITION( UNetworkPredictionComponent, ReplicationProxy_Replay, COND_ReplayOnly);

#if NETSIM_MODEL_DEBUG
	DOREPLIFETIME_CONDITION( UNetworkPredictionComponent, ReplicationProxy_Debug, COND_ReplayOrOwner);
#endif
}

FNetworkSimulationModelInitParameters UNetworkPredictionComponent::GetSimulationInitParameters(ENetRole Role)
{
	// These are reasonable defaults but may not be right for everyone
	FNetworkSimulationModelInitParameters InitParams;
	InitParams.InputBufferSize = Role != ROLE_SimulatedProxy ? 32 : 0;
	InitParams.SyncedBufferSize = Role != ROLE_AutonomousProxy ? 2 : 32;
	InitParams.AuxBufferSize = Role != ROLE_AutonomousProxy ? 2 : 32;
	InitParams.DebugBufferSize = 32;
	InitParams.HistoricBufferSize = 128;
	return InitParams;
}

bool UNetworkPredictionComponent::IsLocallyControlled()
{
	// This awkward because, engine wide, "Is Locally Controlled" is really only a built in concept to Pawns.
	// We don't want the UNetworkPredictionComponent to be explicitly coupled to APawn though.
	// Role isn't enough on its own because standalone/listen servers means Role=ROLE_Authority can be locally controlled.
	// This seems like the best compromise: things will "just work" if the component is a child of an APawn.
	// If you want to have some concept of locally controlled network sims that are not childs of APawn actors, you can always
	// override this function.

	bool bIsLocallyControlled = false;
	if (GetOwner()->Role == ROLE_AutonomousProxy)
	{
		bIsLocallyControlled = true;
	}
	else if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		bIsLocallyControlled = PawnOwner->IsLocallyControlled();
	}

	return bIsLocallyControlled;
}

void UNetworkPredictionComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	CheckOwnerRoleChange();
	if (PreTickLocallyControlledSim.IsBound() && IsLocallyControlled())
	{
		PreTickLocallyControlledSim.Execute(DeltaTime);
	}
}

void UNetworkPredictionComponent::CheckOwnerRoleChange()
{
	ENetRole CurrentRole = GetOwner()->Role;
	if (CurrentRole != OwnerCachedNetRole)
	{
		OwnerCachedNetRole = CurrentRole;
		InitializeForNetworkRole(CurrentRole);		
	}
}

bool UNetworkPredictionComponent::ServerRecieveClientInput_Validate(const FServerReplicationRPCParameter& ProxyParameter)
{
	return true;
}

void UNetworkPredictionComponent::ServerRecieveClientInput_Implementation(const FServerReplicationRPCParameter& ProxyParameter)
{
	// The const_cast is unavoidable here because the replication system only allows by value (forces copy, bad) or by const reference. This use case is unique because we are using the RPC parameter as a temp buffer.
	const_cast<FServerReplicationRPCParameter&>(ProxyParameter).NetSerializeToProxy(ReplicationProxy_ServerRPC);
}