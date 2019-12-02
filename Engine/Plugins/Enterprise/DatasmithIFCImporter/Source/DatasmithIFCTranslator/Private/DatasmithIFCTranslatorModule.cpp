// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithIFCTranslatorModule.h"
#include "DatasmithIFCTranslator.h"

#include "Translators/DatasmithTranslator.h"
#include "DatasmithImporterModule.h"
#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogIFCTranslator);

const TCHAR* IDatasmithIFCTranslatorModule::ModuleName = TEXT("DatasmithIFCTranslator");

class FIFCTranslatorModule : public IDatasmithIFCTranslatorModule
{
public:
	virtual void StartupModule() override
	{
		check(LibHandle == nullptr);

#ifdef WITH_IFC_ENGINE_LIB
		const FString IFCEngineDll = TEXT("ifcengine.dll");

		// determine directory paths
		FString IFCEngineDllPath = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("DatasmithIFCImporter"))->GetBaseDir(), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());
		FPlatformProcess::PushDllDirectory(*IFCEngineDllPath);
		IFCEngineDllPath = FPaths::Combine(IFCEngineDllPath, IFCEngineDll);

		if (!FPaths::FileExists(IFCEngineDllPath))
		{
			UE_LOG(LogIFCTranslator, Error, TEXT("Failed to find the binary folder for the IFCEngine dll. Plug-in will not be functional."));
			return;
		}

		LibHandle = FPlatformProcess::GetDllHandle(*IFCEngineDllPath);

		if (LibHandle == nullptr)
		{
			UE_LOG(LogIFCTranslator, Error, TEXT("Failed to load required library %s. Plug-in will not be functional."), *IFCEngineDllPath);
			return;
		}
#endif

		FModuleManager::Get().LoadModule(TEXT("DatasmithImporter"));
		Datasmith::RegisterTranslator<FDatasmithIFCTranslator>();
	}

	virtual void ShutdownModule() override
	{
		Datasmith::UnregisterTranslator<FDatasmithIFCTranslator>();

		if (LibHandle != nullptr)
		{
			FPlatformProcess::FreeDllHandle(LibHandle);
			LibHandle = nullptr;
		}
	}

private:

	static void* LibHandle;
};

void* FIFCTranslatorModule::LibHandle = nullptr;

IMPLEMENT_MODULE(FIFCTranslatorModule, DatasmithIFCTranslator);
