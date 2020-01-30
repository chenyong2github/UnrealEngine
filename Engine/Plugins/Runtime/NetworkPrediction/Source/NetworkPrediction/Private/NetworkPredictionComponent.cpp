// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "NetworkedSimulationGlobalManager.h"
#include "Engine/World.h"

UNetworkPredictionComponent::UNetworkPredictionComponent()
{
	SetIsReplicatedByDefault(true);
	
	// Tick in order to dispatch NetSimCues. FIXME: Might be useful to have this as an option. Some may want to dispatch cues from actor tick (avoid component ticking at all)
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UNetworkPredictionComponent::InitializeComponent()
{
	Super::InitializeComponent();

	UWorld* World = GetWorld();
	UNetworkSimulationGlobalManager* NetworkSimGlobalManager = GetWorld()->GetSubsystem<UNetworkSimulationGlobalManager>();
	if (NetworkSimGlobalManager)
	{

		// Child class will instantiate
		INetworkedSimulationModel* NewNetworkSim = InstantiateNetworkedSimulation();
		NetSimModel.Reset(NewNetworkSim);

		if (NewNetworkSim == nullptr)
		{
			// Its ok to not instantiate a sim
			return;
		}

		// Init RepProxies
		ReplicationProxy_ServerRPC.Init(NewNetworkSim, EReplicationProxyTarget::ServerRPC);
		ReplicationProxy_Autonomous.Init(NewNetworkSim, EReplicationProxyTarget::AutonomousProxy);
		ReplicationProxy_Simulated.Init(NewNetworkSim, EReplicationProxyTarget::SimulatedProxy);
		ReplicationProxy_Replay.Init(NewNetworkSim, EReplicationProxyTarget::Replay);
	#if NETSIM_MODEL_DEBUG
		ReplicationProxy_Debug.Init(NewNetworkSim, EReplicationProxyTarget::Debug);
	#endif

		// Register with GlobalManager			
		NetworkSimGlobalManager->RegisterModel(NewNetworkSim, GetOwner());
		CheckOwnerRoleChange();
	}
}

void UNetworkPredictionComponent::UninitializeComponent()
{
	Super::UninitializeComponent();

	if (NetSimModel.IsValid())
	{
		UNetworkSimulationGlobalManager* NetworkSimGlobalManager = GetWorld()->GetSubsystem<UNetworkSimulationGlobalManager>();
		if (ensure(NetworkSimGlobalManager))
		{
			NetworkSimGlobalManager->UnregisterModel(NetSimModel.Get(), GetOwner());
			if (ServerRPCHandle.IsValid())
			{
				NetworkSimGlobalManager->TickServerRPCDelegate.Remove(ServerRPCHandle);
			}

			NetSimModel.Reset(nullptr);
		}
	}
}

void UNetworkPredictionComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	if (NetSimModel.IsValid())
	{
		// We have to update our replication proxies so they can be accurately compared against client shadowstate during property replication. ServerRPC proxy does not need to do this.
		ReplicationProxy_Autonomous.OnPreReplication();
		ReplicationProxy_Simulated.OnPreReplication();
		ReplicationProxy_Replay.OnPreReplication();
#if NETSIM_MODEL_DEBUG
		ReplicationProxy_Debug.OnPreReplication();
#endif
	}
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

void UNetworkPredictionComponent::InitializeForNetworkRole(ENetRole Role)
{
	if (NetSimModel.IsValid())
	{
		NetSimModel->InitializeForNetworkRole(Role, GetSimulationInitParameters(Role));
	}
}

FNetworkSimulationModelInitParameters UNetworkPredictionComponent::GetSimulationInitParameters(ENetRole Role)
{
	// These are reasonable defaults but may not be right for everyone
	FNetworkSimulationModelInitParameters InitParams;
	InitParams.InputBufferSize = Role != ROLE_SimulatedProxy ? 32 : 2;
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
	if (GetOwner()->GetLocalRole() == ROLE_AutonomousProxy)
	{
		bIsLocallyControlled = true;
	}
	else if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		bIsLocallyControlled = PawnOwner->IsLocallyControlled();
	}

	return bIsLocallyControlled;
}

bool UNetworkPredictionComponent::CheckOwnerRoleChange()
{
	ENetRole CurrentRole = GetOwner()->GetLocalRole();
	if (CurrentRole != OwnerCachedNetRole)
	{
		if (OwnerCachedNetRole == ROLE_AutonomousProxy)
		{
			UnregisterServerRPCDelegate();
		}

		OwnerCachedNetRole = CurrentRole;
		InitializeForNetworkRole(CurrentRole);

		if (OwnerCachedNetRole == ROLE_AutonomousProxy)
		{
			RegisterServerRPCDelegate();
		}
		return true;
	}

	return false;
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

void UNetworkPredictionComponent::TickServerRPC(float DeltaSeconds)
{
	check(NetSimModel.IsValid());
	if (NetSimModel->ShouldSendServerRPC(DeltaSeconds))
	{
		// Temp hack to make sure the ServerRPC doesn't get suppressed from bandwidth limiting
		// (system hasn't been optimized and not mature enough yet to handle gaps in input stream)
		FScopedBandwidthLimitBypass BandwidthBypass(GetOwner());

		FServerReplicationRPCParameter ProxyParameter(ReplicationProxy_ServerRPC);
		ServerRecieveClientInput(ProxyParameter);
	}
}

void UNetworkPredictionComponent::RegisterServerRPCDelegate()
{
	UNetworkSimulationGlobalManager* NetworkSimGlobalManager = GetWorld()->GetSubsystem<UNetworkSimulationGlobalManager>();
	check(NetworkSimGlobalManager);
	ServerRPCHandle = NetworkSimGlobalManager->TickServerRPCDelegate.AddUObject(this, &UNetworkPredictionComponent::TickServerRPC);
}
void UNetworkPredictionComponent::UnregisterServerRPCDelegate()
{
	if (ServerRPCHandle.IsValid())
	{
		UNetworkSimulationGlobalManager* NetworkSimGlobalManager = GetWorld()->GetSubsystem<UNetworkSimulationGlobalManager>();
		check(NetworkSimGlobalManager);
		NetworkSimGlobalManager->TickServerRPCDelegate.Remove(ServerRPCHandle);
		ServerRPCHandle.Reset();
	}
}

void UNetworkPredictionComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Handle all pending cues (dispatch them to their handler)
	NetSimModel->ProcessPendingNetSimCues();
}