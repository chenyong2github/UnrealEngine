// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "IInterchangePipelinePlugin.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeManager.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"


class FInterchangePipelinePlugin : public IInterchangePipelinePlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FInterchangePipelinePlugin, InterchangePipelinePlugin)



void FInterchangePipelinePlugin::StartupModule()
{
	auto RegisterItems = []()
	{
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
		//Register the Pipeline
		//InterchangeManager.RegisterPipeline(UInterchangeGenericMeshPipeline::StaticClass());
	};

	if (GEngine)
	{
		RegisterItems();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddLambda(RegisterItems);
	}
}


void FInterchangePipelinePlugin::ShutdownModule()
{
	
}



