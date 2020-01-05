// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithDeltaGenTranslatorModule.h"
#include "DatasmithDeltaGenTranslator.h"
#include "MasterMaterials/DatasmithMasterMaterialManager.h"
#include "DatasmithDeltaGenImporterMaterialSelector.h"

#include "Translators/DatasmithTranslator.h"
#include "DatasmithImporterModule.h"
#include "CoreMinimal.h"

class FDeltaGenTranslatorModule : public IDatasmithDeltaGenTranslatorModule
{
public:
	virtual void StartupModule() override
	{
		// Make sure the DatasmithImporter module exists and has been initialized before adding FDatasmithDeltaGenTranslator's material selector
		FModuleManager::Get().LoadModule(TEXT("DatasmithImporter"));

		FDatasmithMasterMaterialManager::Get().RegisterSelector(TEXT("Deltagen"), MakeShared< FDatasmithDeltaGenImporterMaterialSelector >());

		Datasmith::RegisterTranslator<FDatasmithDeltaGenTranslator>();
	}

	virtual void ShutdownModule() override
	{
		Datasmith::UnregisterTranslator<FDatasmithDeltaGenTranslator>();
	}
};

IMPLEMENT_MODULE(FDeltaGenTranslatorModule, DatasmithDeltaGenTranslator);
