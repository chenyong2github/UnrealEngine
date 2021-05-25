// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADInterfacesModule.h"

#include "CoreTechTypes.h"
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

ECADInterfaceAvailability CADInterfaceAvailability = ECADInterfaceAvailability::Unknown;

ECADInterfaceAvailability ICADInterfacesModule::GetAvailability()
{
	if (FModuleManager::Get().IsModuleLoaded(CADINTERFACES_MODULE_NAME))
	{
		if(CADLibrary::CTKIO_InitializeKernel())
		{
			return ECADInterfaceAvailability::Available;
		}
	}

	UE_LOG(CADInterfaces, Warning, TEXT("Failed to load CADInterfaces module. Plug-in may not be functional."));
	return ECADInterfaceAvailability::Unavailable;
}

void FCADInterfacesModule::StartupModule()
{
#if WITH_EDITOR & defined(USE_KERNEL_IO_SDK)
	check(KernelIOLibHandle == nullptr);

	FString KernelIODll = TEXT("kernel_io.dll");

	// determine directory paths
	FString CADImporterDllPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Enterprise/DatasmithCADImporter"), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());
	FPlatformProcess::PushDllDirectory(*CADImporterDllPath);
	KernelIODll = FPaths::Combine(CADImporterDllPath, KernelIODll);

	if (!FPaths::FileExists(KernelIODll))
	{
		UE_LOG(CADInterfaces, Warning, TEXT("CoreTech module is missing. Plug-in will not be functional."));
		return;
	}

	KernelIOLibHandle = FPlatformProcess::GetDllHandle(*KernelIODll);
	if (KernelIOLibHandle == nullptr)
	{
		UE_LOG(CADInterfaces, Warning, TEXT("Failed to load required library %s. Plug-in will not be functional."), *KernelIODll);
	}

	FPlatformProcess::PopDllDirectory(*CADImporterDllPath);

	CADLibrary::InitializeCoreTechInterface();
#endif
}

void FCADInterfacesModule::ShutdownModule()
{
	if (KernelIOLibHandle != nullptr)
	{
#if WITH_EDITOR && defined(USE_KERNEL_IO_SDK)
		// Reset the CoreTechInterface object if compiling for the editor and CoreTech sdk is available
		CADLibrary::SetCoreTechInterface(TSharedPtr<CADLibrary::ICoreTechInterface>());
#endif
		FPlatformProcess::FreeDllHandle(KernelIOLibHandle);
		KernelIOLibHandle = nullptr;
	}
}

IMPLEMENT_MODULE(FCADInterfacesModule, CADInterfaces);

#undef LOCTEXT_NAMESPACE // "CADInterfacesModule"

