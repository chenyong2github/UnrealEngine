// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IInterchangeExportPlugin.h"
#include "InterchangeManager.h"
#include "InterchangeTextureWriter.h"
#include "Engine/Engine.h"

class FInterchangeExportPlugin : public IInterchangeExportPlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FInterchangeExportPlugin, InterchangeExportPlugin)



void FInterchangeExportPlugin::StartupModule()
{
	auto RegisterItems = []()
	{
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
		//Register the Writers
		InterchangeManager.RegisterWriter(UInterchangeTextureWriter::StaticClass());
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


void FInterchangeExportPlugin::ShutdownModule()
{
	
}



