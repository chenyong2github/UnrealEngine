// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionExtrasModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "FNetworkPredictionModule"

class FNetworkPredictionExtrasModule : public INetworkPredictionExtrasModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

void FNetworkPredictionExtrasModule::StartupModule()
{
}


void FNetworkPredictionExtrasModule::ShutdownModule()
{
}

IMPLEMENT_MODULE( FNetworkPredictionExtrasModule, NetworkPredictionExtras )
#undef LOCTEXT_NAMESPACE

