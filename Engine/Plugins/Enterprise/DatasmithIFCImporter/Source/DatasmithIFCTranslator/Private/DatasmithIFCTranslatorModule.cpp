// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithIFCTranslatorModule.h"
#include "DatasmithIFCTranslator.h"

#include "Translators/DatasmithTranslator.h"
#include "DatasmithImporterModule.h"
#include "CoreMinimal.h"

const TCHAR* IDatasmithIFCTranslatorModule::ModuleName = TEXT("DatasmithIFCTranslator");

class FIFCTranslatorModule : public IDatasmithIFCTranslatorModule
{
public:
	virtual void StartupModule() override
	{
		FModuleManager::Get().LoadModule(TEXT("DatasmithImporter"));
		Datasmith::RegisterTranslator<FDatasmithIFCTranslator>();
	}

	virtual void ShutdownModule() override
	{
		Datasmith::UnregisterTranslator<FDatasmithIFCTranslator>();
	}
};

IMPLEMENT_MODULE(FIFCTranslatorModule, DatasmithIFCTranslator);
