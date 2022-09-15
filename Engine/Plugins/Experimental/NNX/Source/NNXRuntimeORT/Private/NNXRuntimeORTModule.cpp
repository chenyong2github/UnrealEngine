// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "NNXRuntime.h"
#include "NNXRuntimeORT.h"
#include "NNXCore.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
//#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

class FNNXRuntimeORTModule : public IModuleInterface
{
public:
	NNX::IRuntime* ORTRuntimeCPU{ nullptr };
	NNX::IRuntime* ORTRuntimeCUDA{ nullptr };
	NNX::IRuntime* ORTRuntimeDML{ nullptr };

	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
void FNNXRuntimeORTModule::StartupModule()
{
	TArray<FString> DLLFileNames
	{
			"onnxruntime",
	};

	//IPluginManager& PluginManager = IPluginManager::Get();
	//const FString PluginDir = PluginManager.FindPlugin(TEXT("NNXIncubator"))->GetBaseDir();
	
	//const FString ORTDefaultModuleDir = PluginDir / "ThirdParty//ORTDefault";
	//const FString ORTDefaultRuntimeBinPath = ORTDefaultModuleDir / TEXT(PREPROCESSOR_TO_STRING(ORTDEFAULT_PLATFORM_PATH));;
	const FString ORTDefaultRuntimeBinPath = TEXT(PREPROCESSOR_TO_STRING(ORTDEFAULT_PLATFORM_BIN_PATH));

	FPlatformProcess::PushDllDirectory(*ORTDefaultRuntimeBinPath);

	TArray<void*> DLLHandles;

	for (FString DLLFileName : DLLFileNames)
	{
		const FString DLLFilePath = ORTDefaultRuntimeBinPath / DLLFileName + ".dll";
		// Sanity check
		if (!FPaths::FileExists(DLLFilePath))
		{
			const FString ErrorMessage = FString::Format(TEXT("DLL file not found in \"{0}\"."),
				{ IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*DLLFilePath) });
			//UE_LOG(LogNNX, Error, TEXT("ORT StartupModule(): %s"), *ErrorMessage);
			//checkf(false, TEXT("%s"), *ErrorMessage);
		}
		DLLHandles.Add(FPlatformProcess::GetDllHandle(*DLLFilePath));

	}
	FPlatformProcess::PopDllDirectory(*ORTDefaultRuntimeBinPath);

 	ORTRuntimeCPU = NNX::FRuntimeORTCPUStartup();
	ORTRuntimeCUDA = NNX::FRuntimeORTCUDAStartup();
	ORTRuntimeDML = NNX::FRuntimeORTDMLStartup();
	if (ORTRuntimeCPU)
	{
		NNX::RegisterRuntime(ORTRuntimeCPU);
	}
	if (ORTRuntimeCUDA)
	{
		NNX::RegisterRuntime(ORTRuntimeCUDA);
	}
	if (ORTRuntimeDML)
	{
		NNX::RegisterRuntime(ORTRuntimeDML);
	}
}

// This function may be called during shutdown to clean up your module. For modules that support dynamic reloading,
// we call this function before unloading the module.
void FNNXRuntimeORTModule::ShutdownModule()
{
	if (ORTRuntimeCPU)
	{
		NNX::UnregisterRuntime(ORTRuntimeCPU);
		ORTRuntimeCPU = nullptr;
	}
	if (ORTRuntimeCUDA)
	{
		NNX::UnregisterRuntime(ORTRuntimeCUDA);
		ORTRuntimeCUDA = nullptr;
	}
	if (ORTRuntimeDML)
	{
		NNX::UnregisterRuntime(ORTRuntimeDML);
		ORTRuntimeDML = nullptr;
	}
	NNX::FRuntimeORTCPUShutdown();
	NNX::FRuntimeORTDMLShutdown();
	NNX::FRuntimeORTCUDAShutdown();
}

IMPLEMENT_MODULE(FNNXRuntimeORTModule, NNXRuntimeORT);
