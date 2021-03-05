// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithCADRuntime.h"

#if PLATFORM_WINDOWS

#include "CoreTechTypes.h"

#include "Modules/ModuleManager.h"

#include "Windows/AllowWindowsPlatformTypes.h"

FString DllPathName;

extern "C"
{
	int32 __declspec(dllexport) DatasmithCADRuntimeInitialize(void (*InitializePtr)(TSharedPtr<CADLibrary::ICoreTechInterface>))
	{
#ifdef USE_KERNEL_IO_SDK
		// Force an explicit load of the Kernel IO library because the delayed load is failing.
		FWindowsPlatformProcess::PushDllDirectory(*DllPathName);
		FString KernelIOPath = DllPathName + TEXT("/kernel_io.dll");
		void* Handle = LoadLibrary(*KernelIOPath);
		FWindowsPlatformProcess::PopDllDirectory(*DllPathName);

		if (Handle != nullptr)
		{
			CADLibrary::InitializeCoreTechInterface();

			TSharedPtr<CADLibrary::ICoreTechInterface> CoreTechInterface = CADLibrary::GetCoreTechInterface();

			CoreTechInterface->SetExternal(true);
			InitializePtr(CoreTechInterface);

			return CoreTechInterface.IsValid() ? 0 : 1;
		}
#endif

		return 1;
	}
}

/** public functions **/
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	// Perform actions based on the reason for calling.
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		// Initialize once for each new process.
		// Return FALSE to fail DLL load.
		{
			TCHAR dllPath[_MAX_PATH];
			::GetModuleFileName(hinstDLL, dllPath, _MAX_PATH);
			TCHAR* LastChar = FCString::Strrchr(dllPath, '\\');
			*LastChar = 0;
			DllPathName = dllPath;
		}
		break;

	case DLL_THREAD_ATTACH:
		// Do thread-specific initialization.
		break;

	case DLL_THREAD_DETACH:
		// Do thread-specific cleanup.
		break;

	case DLL_PROCESS_DETACH:
		// Perform any necessary cleanup.
		break;
	}
	return TRUE;  // Successful DLL_PROCESS_ATTACH.
}

// End Datasmith platform include guard.
#include "Windows/HideWindowsPlatformTypes.h"
#endif

//AB If we don't have to implement a module, or application, and we can save 200kb by not depending on "Projects" module, so borrow some code from UE4Game.cpp to bypass it
TCHAR GInternalProjectName[64] = TEXT("");
IMPLEMENT_FOREIGN_ENGINE_DIR()

