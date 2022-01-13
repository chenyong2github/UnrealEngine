// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGModule.h"
#include "Modules/ModuleInterface.h"

class FPCGModule : public IModuleInterface
{
public:
	//~ IModuleInterface implementation
	virtual void StartupModule() override
	{

	}

	virtual void ShutdownModule() override
	{

	}

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}
	//~ End IModuleInterface implementation
};

IMPLEMENT_MODULE(FPCGModule, PCG);

DEFINE_LOG_CATEGORY(LogPCG);
