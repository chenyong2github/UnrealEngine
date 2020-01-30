// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlatformCryptoTypes.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FPlatformCryptoTypesModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FPlatformCryptoTypesModule, PlatformCryptoTypes)
