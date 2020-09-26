// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareDLL.h"
#include "Modules/ModuleManager.h"
#include "Windows/AllowWindowsPlatformTypes.h"

TCHAR GInternalProjectName[64] = TEXT("TextureShareSDK");
IMPLEMENT_FOREIGN_ENGINE_DIR();

/** public functions **/
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
	// Perform actions based on the reason for calling.
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		// Initialize once for each new process.
		// Return FALSE to fail DLL load.
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

#include "Windows/HideWindowsPlatformTypes.h"
