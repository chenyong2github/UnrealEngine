// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADTranslatorModule.h"

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

	bool bCreateCacheFolder = false;
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CADTranslator.EnableCADCache")))
	{
		bCreateCacheFolder = CVar->GetInt() != 0;
	}

	if (bCreateCacheFolder)
	{
		CacheDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DatasmithCADCache"), *FString::FromInt(CacheVersion)));
		IFileManager::Get().MakeDirectory(*CacheDir);
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

