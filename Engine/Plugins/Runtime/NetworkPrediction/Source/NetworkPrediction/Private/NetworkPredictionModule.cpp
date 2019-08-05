// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "FNetworkPredictionModule"

class FNetworkPredictionModule : public INetworkPredictionModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

void FNetworkPredictionModule::StartupModule()
{
/*
	FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* World, const UWorld::InitializationValues IVS)
	{
		World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([](AActor* Actor)
		{
			if (APawn* Pawn = Cast<APawn>(Actor))
			{

			}
		}));
	});
*/
}


void FNetworkPredictionModule::ShutdownModule()
{
}

IMPLEMENT_MODULE( FNetworkPredictionModule, NetworkPrediction )
#undef LOCTEXT_NAMESPACE

