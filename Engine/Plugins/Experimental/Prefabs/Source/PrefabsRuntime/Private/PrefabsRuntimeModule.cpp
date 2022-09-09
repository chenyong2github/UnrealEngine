// Copyright Epic Games, Inc. All Rights Reserved.

#include "IPrefabsRuntimeModule.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "Templates/Function.h"

DECLARE_LOG_CATEGORY_EXTERN(PrefabsRuntime, Log, All);

class FPrefabsRuntimeModule : public IPrefabsRuntimeModule
{
protected:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FPrefabsRuntimeModule, PrefabsRuntime );

#define LOCTEXT_NAMESPACE "PrefabsRuntime"

void FPrefabsRuntimeModule::StartupModule()
{
}

void FPrefabsRuntimeModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
