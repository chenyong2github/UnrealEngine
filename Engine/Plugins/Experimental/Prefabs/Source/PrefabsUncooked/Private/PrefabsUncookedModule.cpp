// Copyright Epic Games, Inc. All Rights Reserved.

#include "IPrefabsUncookedModule.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "PrefabCompilationManager.h"

DECLARE_LOG_CATEGORY_EXTERN(PrefabsRuntime, Log, All);

class FPrefabsUncookedModule : public IPrefabsUncookedModule
{
protected:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FPrefabsUncookedModule, PrefabsUncooked);

#define LOCTEXT_NAMESPACE "PrefabsUncooked"

void FPrefabsUncookedModule::StartupModule()
{
	FPrefabCompilationManager::Initialize();
}

void FPrefabsUncookedModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
