// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Net/Core/Misc/NetCoreLog.h"

// Force export
#include "Net/Core/Misc/PacketTraits.h"

DEFINE_LOG_CATEGORY(LogNetCore);


class FNetCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}
};


IMPLEMENT_MODULE(FNetCoreModule, NetCore);
