// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "OSCLog.h"

DEFINE_LOG_CATEGORY( OSCLog );

/**
 * The public interface to this module
 */
class FOSCModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FOSCModule, OSC )

void FOSCModule::StartupModule()
{
	if (!FModuleManager::Get().LoadModule(TEXT("Networking")))
	{
		UE_LOG(OSCLog, Error, TEXT("Required module Networking failed to load"));
		return;
	}
}

void FOSCModule::ShutdownModule()
{
}
