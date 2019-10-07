// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpExporterDLL.h"

// SketchUp to Datasmith exporter classes.
#include "DatasmithSketchUpExporterInterface.h"

// Datasmith SDK.
#include "DatasmithExporterManager.h"

DATASMITH_SKETCHUP_EXPORTER_API SketchUpModelExporterInterface* GetSketchUpModelExporterInterface()
{
	return &FDatasmithSketchUpExporterInterface::GetSingleton();
}

#include "windows/allowwindowsplatformtypes.h"

/** public functions **/
BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG FdwReason, LPVOID LpvReserved)
{
	if (FdwReason == DLL_PROCESS_DETACH)
	{
		// Shut down the Datasmith exporter module on exit.
		FDatasmithExporterManager::Shutdown();
	}
	return (TRUE);
}

#include "Windows/HideWindowsPlatformTypes.h"
