// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkSimulationModelDebugger.h"
#include "NetworkSimulationModel.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/NetworkGuid.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"

FNetworkSimulationModelDebuggerManager& FNetworkSimulationModelDebuggerManager::Get()
{
	static FNetworkSimulationModelDebuggerManager Manager;
	return Manager;
}

UObject* FindReplicatedObjectOnPIEServer(UObject* ClientObject)
{
	if (ClientObject == nullptr)
	{
		return nullptr;
	}

	UObject* ServerObject = nullptr;

#if WITH_EDITOR
	if (UWorld* World = ClientObject->GetWorld())
	{
		if (UNetDriver* NetDriver = World->GetNetDriver())
		{
			if (UNetConnection* NetConnection = NetDriver->ServerConnection)
			{
				FNetworkGUID NetGUID = NetConnection->PackageMap->GetNetGUIDFromObject(ClientObject);

				// Find the PIE server world
				for (TObjectIterator<UWorld> It; It; ++It)
				{
					if (It->WorldType == EWorldType::PIE && It->GetNetMode() != NM_Client)
					{
						if (UNetDriver* ServerNetDriver = It->GetNetDriver())
						{
							if (ServerNetDriver->ClientConnections.Num() > 0)
							{
								UPackageMap* ServerPackageMap = ServerNetDriver->ClientConnections[0]->PackageMap;
								ServerObject = ServerPackageMap->GetObjectFromNetGUID(NetGUID, true);
								break;
							}
						}
					}
				}
			}
		}
	}
#endif

	return ServerObject;
}

// ------------------------------------------------------------------------------------------------------------------------
//
//
// ------------------------------------------------------------------------------------------------------------------------

FAutoConsoleCommandWithWorldAndArgs NetworkSimulationModelDebugCmd(TEXT("nms.Debug.LocallyControlledPawn"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& InArgs, UWorld* World) 
{

	if (!World || !World->GetFirstLocalPlayerFromController())
	{
		return;
	}
	ULocalPlayer* Player = World->GetFirstLocalPlayerFromController();
	if (!Player || !Player->GetPlayerController(World) || !Player->GetPlayerController(World)->GetPawn())
	{
		UE_LOG(LogNetworkSimDebug, Error, TEXT("Could not find valid locally controlled pawn."));
		return;
	}

	APawn* Pawn = Player->GetPlayerController(World)->GetPawn();
	FNetworkSimulationModelDebuggerManager::Get().ToggleDebuggerActive(Pawn);
}));

FAutoConsoleCommandWithWorldAndArgs NetworkSimulationModelDebugClassCmd(TEXT("nms.Debug.Class"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* World) 
{
	if (Args.Num() <= 0)
	{
		UE_LOG(LogNetworkSimDebug, Display, TEXT("Usage: nms.Debug.Class <Class>"));
		return;
	}

	UClass* Class = FindObject<UClass>(ANY_PACKAGE, *Args[0]);
	if (Class == nullptr)
	{
		UE_LOG(LogNetworkSimDebug, Display, TEXT("Could not find Class: %s. Searching for partial match."), *Args[0]);

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			if (ClassIt->GetName().Contains(Args[0]))
			{
				Class = *ClassIt;
				UE_LOG(LogNetworkSimDebug, Display, TEXT("Found: %s"), *Class->GetName());
				break;
			}

		}

		if (Class == nullptr)
		{
			UE_LOG(LogNetworkSimDebug, Display, TEXT("No Matches"));
			return;
		}
	}

	// Probably need to do distance based sorting or a way to specify which one you want.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetClass()->IsChildOf(Class))
		{
			UE_LOG(LogNetworkSimDebug, Display, TEXT("Toggling NetworkSim debugger for %s"), *It->GetName());
			FNetworkSimulationModelDebuggerManager::Get().ToggleDebuggerActive(*It);
			break;
		}
	}
	
}));


FAutoConsoleCommandWithWorldAndArgs NetworkSimulationModelDebugToggleContinousCmd(TEXT("nms.Debug.ToggleContinous"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& InArgs, UWorld* World) 
{

	if (!World || !World->GetFirstLocalPlayerFromController())
	{
		return;
	}
	ULocalPlayer* Player = World->GetFirstLocalPlayerFromController();
	if (!Player || !Player->GetPlayerController(World) || !Player->GetPlayerController(World)->GetPawn())
	{
		UE_LOG(LogNetworkSimDebug, Error, TEXT("Could not find valid locally controlled pawn. "));
		return;
	}

	APawn* Pawn = Player->GetPlayerController(World)->GetPawn();
	FNetworkSimulationModelDebuggerManager::Get().SetDebuggerActive(Pawn, true);
	FNetworkSimulationModelDebuggerManager::Get().ToggleContinousGather();
}));