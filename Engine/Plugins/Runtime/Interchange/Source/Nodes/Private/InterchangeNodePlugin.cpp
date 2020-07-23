// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IInterchangeNodePlugin.h"
//#include "InterchangeManager.h"

DEFINE_LOG_CATEGORY(LogInterchangeNodePlugin);

class FInterchangeNodePlugin : public IInterchangeNodePlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FInterchangeNodePlugin, InterchangeNodePlugin)



void FInterchangeNodePlugin::StartupModule()
{
	//Register anything needed to the interchange manager
	//UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
	//InterchangeManager.Register...
}


void FInterchangeNodePlugin::ShutdownModule()
{
	
}



