// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blast/BlastCorePlugin.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"




class FBlastCorePlugin : public IBlastCorePlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FBlastCorePlugin, BlastCore )



void FBlastCorePlugin::StartupModule()
{
	
}


void FBlastCorePlugin::ShutdownModule()
{
	
}



