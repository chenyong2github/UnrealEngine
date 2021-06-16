// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FChaosEngineModule, Chaos);

void FChaosEngineModule::StartupModule()
{
	if (FModuleManager::Get().ModuleExists(TEXT("GeometryCore")))
	{
		FModuleManager::Get().LoadModule("GeometryCore");
	}
}
