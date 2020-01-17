// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADInterfacesModule.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "CADInterfacesModule"

DEFINE_LOG_CATEGORY(CADInterfaces);

class FCADInterfacesModule : public ICADInterfacesModule
{
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void* KernelIOLibHandle;
};

void* FCADInterfacesModule::KernelIOLibHandle = nullptr;

ICADInterfacesModule& ICADInterfacesModule::Get()
{
	return FModuleManager::LoadModuleChecked< FCADInterfacesModule >(CADINTERFACES_MODULE_NAME);
}

bool ICADInterfacesModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(CADINTERFACES_MODULE_NAME);
}

void FCADInterfacesModule::StartupModule()
{
	check(KernelIOLibHandle == nullptr);

	FString KernelIODll = TEXT("kernel_io.dll");

	// determine directory paths
	FString CADImporterDllPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Enterprise/DatasmithCADImporter"), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());
	FPlatformProcess::PushDllDirectory(*CADImporterDllPath);
	KernelIODll = FPaths::Combine(CADImporterDllPath, KernelIODll);

	if (!FPaths::FileExists(KernelIODll))
	{
		UE_LOG(CADInterfaces, Error, TEXT("Failed to find the binary folder for the CoreTech dll. Plug-in will not be functional."));
		return;
	}

	KernelIOLibHandle = FPlatformProcess::GetDllHandle(*KernelIODll);
	if (KernelIOLibHandle == nullptr)
	{
		UE_LOG(CADInterfaces, Error, TEXT("Failed to load required library %s. Plug-in will not be functional."), *KernelIODll);
	}

	FPlatformProcess::PopDllDirectory(*CADImporterDllPath);
}

void FCADInterfacesModule::ShutdownModule()
{
	if (KernelIOLibHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(KernelIOLibHandle);
		KernelIOLibHandle = nullptr;
	}
}

IMPLEMENT_MODULE(FCADInterfacesModule, CADInterfaces);

#undef LOCTEXT_NAMESPACE // "CADInterfacesModule"

