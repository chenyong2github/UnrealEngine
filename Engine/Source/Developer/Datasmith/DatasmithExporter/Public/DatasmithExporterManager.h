// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

class DATASMITHEXPORTER_API FDatasmithExporterManager
{
public:
	/**
	 * Initializes the Datasmith Exporter module.
	 * @param LogFilename specifies the file where to store the text logged.
	 * Needs to be called before starting any export.
	 * Must be called once
	 *
	 * @return True if the initialization was successful
	 */
	static bool Initialize();

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
