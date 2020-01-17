// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithTranslatorModule.h"

#include "MasterMaterials/DatasmithC4DMaterialSelector.h"
#include "MasterMaterials/DatasmithCityEngineMaterialSelector.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "MasterMaterials/DatasmithRevitMaterialSelector.h"
#include "MasterMaterials/DatasmithSketchupMaterialSelector.h"

void IDatasmithTranslatorModule::StartupModule()
{
	FDatasmithMasterMaterialManager::Create();

	//A minimal set of natively supported master materials
	FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("C4D"), MakeShared< FDatasmithC4DMaterialSelector >());
	FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("Revit"), MakeShared< FDatasmithRevitMaterialSelector >());
	FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("SketchUp"), MakeShared< FDatasmithSketchUpMaterialSelector >());
	FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("CityEngine"), MakeShared< FDatasmithCityEngineMaterialSelector >());
}

void IDatasmithTranslatorModule::ShutdownModule()
{
	FDatasmithMasterMaterialManager::Get().UnregisterSelector(TEXT("C4D"));
	FDatasmithMasterMaterialManager::Get().UnregisterSelector(TEXT("Revit"));
	FDatasmithMasterMaterialManager::Get().UnregisterSelector(TEXT("SketchUp"));
	FDatasmithMasterMaterialManager::Get().UnregisterSelector(TEXT("CityEngine"));

	FDatasmithMasterMaterialManager::Destroy();
}

IMPLEMENT_MODULE(IDatasmithTranslatorModule, DatasmithTranslator);