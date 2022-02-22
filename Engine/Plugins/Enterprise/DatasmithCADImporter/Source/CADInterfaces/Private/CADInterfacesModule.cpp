// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADInterfacesModule.h"

#include "CADOptions.h"
#include "CoreTechTypes.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TechSoftInterface.h"

#define LOCTEXT_NAMESPACE "CADInterfacesModule"

DEFINE_LOG_CATEGORY(LogCADInterfaces);

class FCADInterfacesModule : public ICADInterfacesModule
{
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void* KernelIOLibHandle;
	static void* TechSoftLibHandle;
};

void* FCADInterfacesModule::KernelIOLibHandle = nullptr;
void* FCADInterfacesModule::TechSoftLibHandle = nullptr;

ICADInterfacesModule& ICADInterfacesModule::Get()
{
	return FModuleManager::LoadModuleChecked< FCADInterfacesModule >(CADINTERFACES_MODULE_NAME);
}

ECADInterfaceAvailability CADInterfaceAvailability = ECADInterfaceAvailability::Unknown;

ECADInterfaceAvailability ICADInterfacesModule::GetAvailability()
{
	if (FModuleManager::Get().IsModuleLoaded(CADINTERFACES_MODULE_NAME))
	{
		if (CADLibrary::FImportParameters::GCADLibrary == TEXT("KernelIO"))
		{
			if (CADLibrary::CTKIO_InitializeKernel())
			{
				return ECADInterfaceAvailability::Available;
			}
		}
		else if (CADLibrary::FImportParameters::GCADLibrary == TEXT("TechSoft"))
		{
			if (CADLibrary::TechSoftInterface::TECHSOFT_InitializeKernel())
			{
				return ECADInterfaceAvailability::Available;
			}
		}
	}

	UE_LOG(LogCADInterfaces, Warning, TEXT("Failed to load CADInterfaces module. Plug-in may not be functional."));
	return ECADInterfaceAvailability::Unavailable;
}

void FCADInterfacesModule::StartupModule()
{
	// determine directory paths
	FString CADImporterDllPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Enterprise/DatasmithCADImporter"), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory());
	FPlatformProcess::PushDllDirectory(*CADImporterDllPath);

#if WITH_EDITOR & defined(USE_KERNEL_IO_SDK)
	check(KernelIOLibHandle == nullptr);

	FString KernelIODll = TEXT("kernel_io.dll");
	KernelIODll = FPaths::Combine(CADImporterDllPath, KernelIODll);

	if (!FPaths::FileExists(KernelIODll))
	{
		UE_LOG(LogCADInterfaces, Warning, TEXT("CoreTech module is missing. Plug-in will not be functional."));
	}
	else
	{
		KernelIOLibHandle = FPlatformProcess::GetDllHandle(*KernelIODll);
		if (KernelIOLibHandle == nullptr)
		{
			UE_LOG(LogCADInterfaces, Warning, TEXT("Failed to load required library %s. Plug-in will not be functional."), *KernelIODll);
		}
		else
		{
			CADLibrary::InitializeCoreTechInterface();
		}

		FPlatformProcess::PopDllDirectory(*CADImporterDllPath);
	}
#endif

#if WITH_EDITOR & defined(USE_TECHSOFT_SDK)
	check(TechSoftLibHandle == nullptr);

	FString TechSoftDllPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(CADImporterDllPath, "TechSoft"));
	FPlatformProcess::PushDllDirectory(*TechSoftDllPath);

#if PLATFORM_WINDOWS
	FString TechSoftDll = TEXT("A3DLIBS.dll");
#elif PLATFORM_LINUX
	FString TechSoftDll = TEXT("libA3DLIBS.so");
#else
#error Platform not supported
#endif
	TechSoftDll = FPaths::Combine(TechSoftDllPath, TechSoftDll);

	if (!FPaths::FileExists(TechSoftDll))
	{
		UE_LOG(LogCADInterfaces, Warning, TEXT("TechSoft module is missing. Plug-in will not be functional."));
	}
	else
	{
		TechSoftLibHandle = FPlatformProcess::GetDllHandle(*TechSoftDll);
		if (TechSoftLibHandle == nullptr)
		{
			UE_LOG(LogCADInterfaces, Warning, TEXT("Failed to load required library %s. Plug-in will not be functional."), *TechSoftDll);
		}

		FPlatformProcess::PopDllDirectory(*TechSoftDllPath);
	}
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

