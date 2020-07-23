// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IInterchangeExportPlugin.h"
#include "InterchangeManager.h"
#include "InterchangeTextureWriter.h"

class FInterchangeExportPlugin : public IInterchangeExportPlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FInterchangeExportPlugin, InterchangeExportPlugin)



void FInterchangeExportPlugin::StartupModule()
{
	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

	//Register the Writers
	InterchangeManager.RegisterWriter(UInterchangeTextureWriter::StaticClass());
}


void FInterchangeExportPlugin::ShutdownModule()
{
	
}



