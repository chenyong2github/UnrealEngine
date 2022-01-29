// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADTranslatorModule.h"

#include "CADOptions.h"
#include "CADToolsModule.h"
#include "DatasmithCADTranslator.h"

#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


IMPLEMENT_MODULE(FDatasmithCADTranslatorModule, DatasmithCADTranslator);

void FDatasmithCADTranslatorModule::StartupModule()
{
	const int32 CacheVersion = FCADToolsModule::Get().GetCacheVersion();

	// Create temporary directory which will be used by CoreTech to store tessellation data
	for (int32 Version = 0; Version < CacheVersion; ++Version)
	{
		FString OldCacheDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DatasmithCADCache"), *FString::FromInt(Version)));
		IFileManager::Get().DeleteDirectory(*OldCacheDir, true, true);
	}

	CacheDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DatasmithCADCache"), *FString::FromInt(CacheVersion)));
	if (!IFileManager::Get().MakeDirectory(*CacheDir, true))
	{
		CacheDir.Empty();
		CADLibrary::FImportParameters::bGEnableCADCache = false; // very weak protection: user could turn that on later, while the cache path is invalid
	}

	// Create body cache directory since this one is used even if bGEnableCADCache is false
	if (!CacheDir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*FPaths::Combine(CacheDir, TEXT("body")), true);
	}

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

