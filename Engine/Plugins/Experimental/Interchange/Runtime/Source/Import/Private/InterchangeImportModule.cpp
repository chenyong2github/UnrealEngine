// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeImportModule.h"

#include "CoreMinimal.h"
#include "Fbx/InterchangeFbxTranslator.h"
#include "InterchangeImportLog.h"
#include "InterchangeManager.h"
#include "Material/InterchangeMaterialFactory.h"
#include "Mesh/InterchangePhysicsAssetFactory.h"
#include "Mesh/InterchangeSkeletalMeshFactory.h"
#include "Mesh/InterchangeSkeletonFactory.h"
#include "Mesh/InterchangeStaticMeshFactory.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Scene/InterchangeActorFactory.h"
#include "Texture/InterchangeBMPTranslator.h"
#include "Texture/InterchangeDDSTranslator.h"
#include "Texture/InterchangeEXRTranslator.h"
#include "Texture/InterchangeHDRTranslator.h"
#include "Texture/InterchangeIESTranslator.h"
#include "Texture/InterchangeJPGTranslator.h"
#include "Texture/InterchangePCXTranslator.h"
#include "Texture/InterchangePNGTranslator.h"
#include "Texture/InterchangePSDTranslator.h"
#include "Texture/InterchangeTextureFactory.h"
#include "Texture/InterchangeTGATranslator.h"
#include "Texture/InterchangeTIFFTranslator.h"
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
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeUDIMTranslator::StaticClass());

		InterchangeManager.RegisterTranslator(UInterchangeBMPTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeBMPTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeDDSTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeDDSTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeEXRTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeEXRTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeJPGTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeJPGTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePCXTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangePCXTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePNGTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangePNGTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangePSDTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangePSDTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeTGATranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeTGATranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeHDRTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeHDRTranslator::StaticClass());
		InterchangeManager.RegisterTranslator(UInterchangeIESTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeIESTranslator::StaticClass());
#if WITH_LIBTIFF
		InterchangeManager.RegisterTranslator(UInterchangeTIFFTranslator::StaticClass());
		InterchangeManager.RegisterTextureOnlyTranslatorClass(UInterchangeTIFFTranslator::StaticClass());
#endif // WITH_LIBTIFF

		//Register the factories
		InterchangeManager.RegisterFactory(UInterchangeTextureFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeMaterialFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeSkeletonFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeSkeletalMeshFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeStaticMeshFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangePhysicsAssetFactory::StaticClass());
		InterchangeManager.RegisterFactory(UInterchangeActorFactory::StaticClass());
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



