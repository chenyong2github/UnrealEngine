// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * Currently empty implementation for InstallBundleModule until things are moved in here.
 */
class FInstallBundleModule : public IModuleInterface
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};
