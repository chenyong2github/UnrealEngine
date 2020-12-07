// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Fbx/InterchangeFbxTranslator.h"
#include "IInterchangeImportPlugin.h"
#include "InterchangeManager.h"
#include "Material/InterchangeMaterialFactory.h"
#include "Mesh/InterchangeSkeletalMeshFactory.h"
#include "Mesh/InterchangeSkeletonFactory.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Texture/InterchangeBMPTranslator.h"
#include "Texture/InterchangeDDSTranslator.h"
#include "Texture/InterchangeEXRTranslator.h"
#include "Texture/InterchangeJPGTranslator.h"
#include "Texture/InterchangePCXTranslator.h"
#include "Texture/InterchangePNGTranslator.h"
#include "Texture/InterchangePSDTranslator.h"
#include "Texture/InterchangeTextureFactory.h"
#include "Texture/InterchangeTGATranslator.h"

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
	auto RegisterItems = []()
	{
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

		//Register the translators
		//Scenes
		InterchangeManager.RegisterTranslator(UInterchangeFbxTranslator::StaticClass()); //Do not submit uncommented until we replace completly fbx importer (staticmesh + skeletalMesh + animation)
		//Textures
		InterchangeManager.RegisterTranslator(UInterchangeBMPTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeDDSTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeEXRTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeJPGTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePCXTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePNGTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePSDTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeTGATranslator::StaticClass());

		//Register the factories
		InterchangeManager.RegisterFactory(UInterchangeTextureFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeMaterialFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeSkeletonFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeSkeletalMeshFactory::StaticClass());
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


void FInterchangeImportPlugin::ShutdownModule()
{
	
}



