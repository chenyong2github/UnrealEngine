// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDTestsModule.h"

#include "USDMemory.h"

#include "Modules/ModuleManager.h"

class FUsdTestsModule : public IUsdTestsModule
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE_USD( FUsdTestsModule, USDTests );
