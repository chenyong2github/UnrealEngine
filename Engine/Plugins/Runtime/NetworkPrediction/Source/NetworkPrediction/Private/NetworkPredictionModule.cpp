// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "Engine/World.h"
#include "NetworkedSimulationModelCues.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "FNetworkPredictionModule"

class FNetworkPredictionModule : public INetworkPredictionModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

void FNetworkPredictionModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddLambda([]()
	{
		FGlobalCueTypeTable::Get().FinalizeTypes();
	});
}


void FNetworkPredictionModule::ShutdownModule()
{
}

IMPLEMENT_MODULE( FNetworkPredictionModule, NetworkPrediction )
#undef LOCTEXT_NAMESPACE

