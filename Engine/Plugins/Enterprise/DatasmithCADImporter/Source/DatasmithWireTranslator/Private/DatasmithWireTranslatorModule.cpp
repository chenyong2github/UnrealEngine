// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithWireTranslatorModule.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "DatasmithWireTranslator.h"

IMPLEMENT_MODULE(FDatasmithWireTranslatorModule, DatasmithWireTranslator);

void FDatasmithWireTranslatorModule::StartupModule()
{
	// Create temporary directory which will be used by CoreTech to store tessellation data
	TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("WireImportTemp"));
	IFileManager::Get().MakeDirectory(*TempDir);

	Datasmith::RegisterTranslator<FDatasmithWireTranslator>();
}

void FDatasmithWireTranslatorModule::ShutdownModule()
{
	Datasmith::UnregisterTranslator<FDatasmithWireTranslator>();
}

FString FDatasmithWireTranslatorModule::GetTempDir() const
{
	return TempDir;
}
