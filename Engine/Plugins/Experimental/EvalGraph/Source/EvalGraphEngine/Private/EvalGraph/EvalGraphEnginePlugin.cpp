// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphEnginePlugin.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"



class FEvalGraphEnginePlugin : public IEvalGraphEnginePlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FEvalGraphEnginePlugin, EvalGraphEngine )


void FEvalGraphEnginePlugin::StartupModule()
{
	
}


void FEvalGraphEnginePlugin::ShutdownModule()
{
	
}



