// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IInterchangeImportPlugin.h"
#include "InterchangeManager.h"
#include "Modules/ModuleManager.h"
#include "Texture/InterchangeTextureFactory.h"
#include "Texture/InterchangePNGTranslator.h"
#include "Texture/InterchangeJPGTranslator.h"

DEFINE_LOG_CATEGORY(LogInterchangeImportPlugin);

class FInterchangeImportPlugin : public IInterchangeImportPlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE( FInterchangeImportPlugin, InterchangeImportPlugin)



void FInterchangeImportPlugin::StartupModule()
{
	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

	//Register the translators
	InterchangeManager.RegisterTranslator(UInterchangePNGTranslator::StaticClass());
	InterchangeManager.RegisterTranslator(UInterchangeJPGTranslator::StaticClass());

	//Register the factories
	InterchangeManager.RegisterFactory(UInterchangeTextureFactory::StaticClass());
}


void FInterchangeImportPlugin::ShutdownModule()
{
	
}



