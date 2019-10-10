// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithGLTFTranslatorModule.h"
#include "DatasmithGLTFTranslator.h"

#include "Translators/DatasmithTranslator.h"
#include "DatasmithImporterModule.h"
#include "CoreMinimal.h"

const TCHAR* IDatasmithGLTFTranslatorModule::ModuleName = TEXT("DatasmithGLTFTranslator");

class FGLTFTranslatorModule : public IDatasmithGLTFTranslatorModule
{
public:
	virtual void StartupModule() override
	{
		FModuleManager::Get().LoadModule(TEXT("DatasmithImporter"));
		Datasmith::RegisterTranslator<FDatasmithGLTFTranslator>();
	}

	virtual void ShutdownModule() override
	{
		Datasmith::UnregisterTranslator<FDatasmithGLTFTranslator>();
	}
};

IMPLEMENT_MODULE(FGLTFTranslatorModule, DatasmithGLTFTranslator);
