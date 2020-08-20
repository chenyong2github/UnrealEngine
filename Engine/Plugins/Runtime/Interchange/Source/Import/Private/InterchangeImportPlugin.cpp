// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "IInterchangeImportPlugin.h"
#include "InterchangeManager.h"
#include "Modules/ModuleManager.h"
#include "Texture/InterchangeJPGTranslator.h"
#include "Texture/InterchangePCXTranslator.h"
#include "Texture/InterchangePNGTranslator.h"
#include "Texture/InterchangeTextureFactory.h"


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

	//Textures
	InterchangeManager.RegisterTranslator(UInterchangePNGTranslator::StaticClass());
	InterchangeManager.RegisterTranslator(UInterchangeJPGTranslator::StaticClass());
	InterchangeManager.RegisterTranslator(UInterchangePCXTranslator::StaticClass());

	//Register the factories
	InterchangeManager.RegisterFactory(UInterchangeTextureFactory::StaticClass());
}


void FInterchangeImportPlugin::ShutdownModule()
{
	
}



