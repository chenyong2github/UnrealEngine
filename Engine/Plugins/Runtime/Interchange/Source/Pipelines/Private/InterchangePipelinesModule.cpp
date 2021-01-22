// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangePipelinesModule.h"

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeManager.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"


class FInterchangePipelinesModule : public IInterchangePipelinesModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FInterchangePipelinesModule, InterchangePipelines)



void FInterchangePipelinesModule::StartupModule()
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


void FInterchangePipelinesModule::ShutdownModule()
{

}



