// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeImportModule.h"

#include "CoreMinimal.h"
#include "Fbx/InterchangeFbxTranslator.h"
#include "InterchangeImportLog.h"
#include "InterchangeManager.h"
#include "Material/InterchangeMaterialFactory.h"
#include "Mesh/InterchangeSkeletalMeshFactory.h"
#include "Mesh/InterchangeSkeletonFactory.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Texture/InterchangeBMPTranslator.h"
#include "Texture/InterchangeDDSTranslator.h"
#include "Texture/InterchangeEXRTranslator.h"
#include "Texture/InterchangeHDRTranslator.h"
#include "Texture/InterchangeJPGTranslator.h"
#include "Texture/InterchangePCXTranslator.h"
#include "Texture/InterchangePNGTranslator.h"
#include "Texture/InterchangePSDTranslator.h"
#include "Texture/InterchangeTextureFactory.h"
#include "Texture/InterchangeTGATranslator.h"
#include "Texture/InterchangeUDIMTranslator.h"

DEFINE_LOG_CATEGORY(LogInterchangeImport);

class FInterchangeImportModule : public IInterchangeImportModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FInterchangeImportModule, InterchangeImport)



void FInterchangeImportModule::StartupModule()
{
	auto RegisterItems = []()
	{
		UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

		//Register the translators
		//Scenes
		InterchangeManager.RegisterTranslator(UInterchangeFbxTranslator::StaticClass()); //Do not submit uncommented until we replace completly fbx importer (staticmesh + skeletalMesh + animation)

		//Textures

		// UDIM must be registered before the other texture translators
		InterchangeManager.RegisterTranslator(UInterchangeUDIMTranslator::StaticClass());


		InterchangeManager.RegisterTranslator(UInterchangeBMPTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeDDSTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeEXRTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeJPGTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePCXTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePNGTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePSDTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeTGATranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeHDRTranslator::StaticClass());

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


void FInterchangeImportModule::ShutdownModule()
{

}



