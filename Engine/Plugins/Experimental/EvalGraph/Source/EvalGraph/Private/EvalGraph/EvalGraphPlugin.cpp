// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphPlugin.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"



class FEvalGraphPlugin : public IEvalGraphPlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FEvalGraphPlugin, EvalGraph )


void FEvalGraphPlugin::StartupModule()
{
	
}


void FEvalGraphPlugin::ShutdownModule()
{
	
}



