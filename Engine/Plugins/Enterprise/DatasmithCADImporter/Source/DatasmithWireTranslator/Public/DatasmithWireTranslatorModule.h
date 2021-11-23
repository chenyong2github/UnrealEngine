// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#if defined(OPEN_MODEL_2020)
#define DATASMITHWIRETRANSLATOR_MODULE_NAME TEXT("DatasmithWireTranslator2020")
#elif defined(OPEN_MODEL_2021_3)
#define DATASMITHWIRETRANSLATOR_MODULE_NAME TEXT("DatasmithWireTranslator2021_3")
#elif defined(OPEN_MODEL_2022)
#define DATASMITHWIRETRANSLATOR_MODULE_NAME TEXT("DatasmithWireTranslator2022")
#elif defined(OPEN_MODEL_2022_1)
#define DATASMITHWIRETRANSLATOR_MODULE_NAME TEXT("DatasmithWireTranslator2022_1")
#elif defined(OPEN_MODEL_2022_2)
#define DATASMITHWIRETRANSLATOR_MODULE_NAME TEXT("DatasmithWireTranslator2022_2")
#endif

/**
 * Datasmith Translator for .wire files.
 */
class FDatasmithWireTranslatorModule : public IModuleInterface
{
public:

    static FDatasmithWireTranslatorModule& Get()
    {
        return FModuleManager::LoadModuleChecked< FDatasmithWireTranslatorModule >(DATASMITHWIRETRANSLATOR_MODULE_NAME);
	}

    static bool IsAvailable()
    {
        return FModuleManager::Get().IsModuleLoaded(DATASMITHWIRETRANSLATOR_MODULE_NAME);
    }

	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	FString GetTempDir() const;

private:
	FString TempDir;
};
