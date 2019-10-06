// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Datasmith SDK.
#include "DatasmithExporterManager.h"

// Begin Datasmith platform include gard.
#include "Windows/AllowWindowsPlatformTypes.h"

BOOL WINAPI DllMain(
	HINSTANCE HinstDLL,
	DWORD     FdwReason,
	LPVOID    LpvReserved
)
{
	if (FdwReason == DLL_PROCESS_DETACH)
	{
		// When a process is unloading the DLL, shut down the Datasmith exporter module.
		FDatasmithExporterManager::Shutdown();
	}
	
	return TRUE;
}

// End Datasmith platform include guard.
#include "Windows/HideWindowsPlatformTypes.h"
