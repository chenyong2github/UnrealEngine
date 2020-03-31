// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkedSimulationGlobalManager.h"
#include "Engine/World.h"
#include "Trace/NetworkPredictionTrace.h"

DEFINE_LOG_CATEGORY_STATIC(NetworkSimulationGlobalManager, Log, All);

UNetworkSimulationGlobalManager::UNetworkSimulationGlobalManager()
{

}

void UNetworkSimulationGlobalManager::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = GetWorld();
	check(World);

	PreTickDispatchHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &UNetworkSimulationGlobalManager::OnWorldPreTick);
	PostTickDispatchHandle = World->OnPostTickDispatch().AddUObject(this, &UNetworkSimulationGlobalManager::ReconcileSimulationsPostNetworkUpdate);
	PreWorldActorTickHandle = FWorldDelegates::OnWorldPreActorTick.AddUObject(this, &UNetworkSimulationGlobalManager::BeginNewSimulationFrame);
}

void UNetworkSimulationGlobalManager::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->OnPostTickDispatch().Remove(PostTickDispatchHandle);
	}
	
	FWorldDelegates::OnWorldTickStart.Remove(PreTickDispatchHandle);
	FWorldDelegates::OnWorldPreActorTick.Remove(PreWorldActorTickHandle);
}

void UNetworkSimulationGlobalManager::OnWorldPreTick(UWorld* InWorld, ELevelTick InLevelTick, float InDeltaSeconds)
{
	if (InWorld != GetWorld() || !InWorld->HasBegunPlay())
	{
		return;
	}

	UE_NP_TRACE_WORLD_FRAME_START(InDeltaSeconds);
}

void UNetworkSimulationGlobalManager::ReconcileSimulationsPostNetworkUpdate()
{
	for (auto It = SimulationGroupMap.CreateIterator(); It; ++It)
	{
		for (auto& SimItem : It.Value().Simulations)
		{
			SimItem.Simulation->Reconcile(SimItem.OwningActor->GetLocalRole());
		}
	}
}

void UNetworkSimulationGlobalManager::BeginNewSimulationFrame(UWorld* InWorld, ELevelTick InLevelTick, float InDeltaSeconds)
{
	if (InWorld != GetWorld() || !InWorld->HasBegunPlay())
	{
		return;
	}

	FNetSimTickParameters TickParameters(InDeltaSeconds);

	for (auto It = SimulationGroupMap.CreateIterator(); It; ++It)
	{
		for (auto& SimItem : It.Value().Simulations)
		{
			TickParameters.InitFromActor(SimItem.OwningActor);
			SimItem.Simulation->Tick(TickParameters);
		}
	}

	TickServerRPCDelegate.Broadcast(InDeltaSeconds);
}

void UNetworkSimulationGlobalManager::RegisterModel(INetworkedSimulationModel* Model, AActor* OwningActor)
{
	SimulationGroupMap.FindOrAdd(Model->GetSimulationGroupName()).Simulations.Emplace(Model, OwningActor);
}

void UNetworkSimulationGlobalManager::UnregisterModel(INetworkedSimulationModel* Model, AActor* OwningActor)
{
	auto& SimulationList = SimulationGroupMap.FindOrAdd(Model->GetSimulationGroupName()).Simulations;
	int32 idx = SimulationList.IndexOfByKey(Model);
	if (idx != INDEX_NONE)
	{
		SimulationList.RemoveAtSwap(idx, 1, false);
	}
	else
	{
		UE_LOG(NetworkSimulationGlobalManager, Warning, TEXT("::Unregister model called on model %s but could not be found in manager."), *Model->GetSimulationGroupName().ToString());
	}
}