// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"


class IMsQuicRuntimeModule
	: public IModuleInterface
{

public:

	/**
	 * Initiates the MsQuic runtime, loads the appropriate DLLs.
	 */
	virtual bool InitRuntime() = 0;

};
