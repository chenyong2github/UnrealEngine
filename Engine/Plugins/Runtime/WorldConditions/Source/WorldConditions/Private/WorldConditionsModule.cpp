// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionsModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "WorldConditions"

class FWorldConditionsModule : public IWorldConditionsModule
{
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FWorldConditionsModule, WorldConditionsModule)

void FWorldConditionsModule::StartupModule()
{
}

void FWorldConditionsModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
