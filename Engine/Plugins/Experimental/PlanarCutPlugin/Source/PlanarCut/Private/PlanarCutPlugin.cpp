// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PlanarCutPlugin.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"




class FPlanarCutPlugin : public IPlanarCutPlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FPlanarCutPlugin, PlanarCut )



void FPlanarCutPlugin::StartupModule()
{
	
}


void FPlanarCutPlugin::ShutdownModule()
{
	
}



