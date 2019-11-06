// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADTranslatorModule.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "DatasmithCADTranslator.h"

IMPLEMENT_MODULE(FDatasmithCADTranslatorModule, DatasmithCADTranslator);

void FDatasmithCADTranslatorModule::StartupModule()
{
	// Create temporary directory which will be used by CoreTech to store tessellation data
	const TCHAR* CacheVersion = TEXT("0");
	CacheDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DatasmithCADCache"), CacheVersion));
	IFileManager::Get().MakeDirectory(*CacheDir);

	Datasmith::RegisterTranslator<FDatasmithCADTranslator>();
}

void FDatasmithCADTranslatorModule::ShutdownModule()
{
	Datasmith::UnregisterTranslator<FDatasmithCADTranslator>();
}


FString FDatasmithCADTranslatorModule::GetCacheDir() const
{
	return CacheDir;
}

