// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

class DATASMITHEXPORTER_API FDatasmithExporterManager
{
public:
	struct FInitOptions
	{
		bool bSuppressLogs = true;
		bool bSaveLogToUserDir = true;
		bool bEnableMessaging = false;
	};

	/**
	 * Initializes the Datasmith Exporter module.
	 * @param LogFilename specifies the file where to store the text logged.
	 * Needs to be called before starting any export.
	 * Must be called once
	 *
	 * @return True if the initialization was successful
	 */
	static bool Initialize();
	static bool Initialize(const FInitOptions& InitOptions);

	/**
	 * Shuts down the Datasmith Exporter module.
	 * Must be called when the process performing exports exits
	 */
	static void Shutdown();

#if IS_PROGRAM
private:
	static bool bEngineInitialized;
#endif
};
