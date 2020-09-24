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

ECADInterfaceAvailability ICADInterfacesModule::IsAvailable()
{
	if (CADInterfaceAvailability != ECADInterfaceAvailability::Unknown)
	{
		return CADInterfaceAvailability;
	}

	CADInterfaceAvailability = ECADInterfaceAvailability::Unavailable;

	if (!FModuleManager::Get().IsModuleLoaded(CADINTERFACES_MODULE_NAME))
	{
		UE_LOG(CADInterfaces, Error, TEXT("Failed to load CADInterfaces module. Plug-in will not be functional."));
		return CADInterfaceAvailability;
	}

#ifdef CAD_INTERFACE
	double MetricUnit = 0.001;
	CT_IO_ERROR InitalizationStatus = CADLibrary::CTKIO_InitializeKernel(MetricUnit);
	if (InitalizationStatus == IO_OK || InitalizationStatus == IO_ERROR_ALREADY_INITIALIZED)
	{
		CADInterfaceAvailability = ECADInterfaceAvailability::Available;
	}
	else
	{
		switch (InitalizationStatus)
		{
		case IO_ERROR_LICENSE:
			UE_LOG(CADInterfaces, Error, TEXT("CoreTech dll license is missing. Plug - in will not be functional."));
			break;
		case IO_ERROR_NOT_INITIALIZED:
		default:
			UE_LOG(CADInterfaces, Error, TEXT("CoreTech dll is not initialize. Plug - in will not be functional."));
			break;
		}
	}
#endif
	return CADInterfaceAvailability;
}

void FCADInterfacesModule::StartupModule()
{
	check(KernelIOLibHandle == nullptr);

#ifdef CAD_INTERFACE
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
#else
	UE_LOG(CADInterfaces, Display, TEXT("Missing CoreTech module. Plug-in will not be functional."));
#endif
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

