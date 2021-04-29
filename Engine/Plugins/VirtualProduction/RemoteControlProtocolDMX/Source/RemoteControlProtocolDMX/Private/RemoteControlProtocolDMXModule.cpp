// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "IRemoteControlProtocolModule.h"
#include "RemoteControlProtocolDMX.h"

/**
 * DMX remote control module
 */
class FRemoteControlProtocolDMXModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override
	{
		IRemoteControlProtocolModule::Get().AddProtocol("DMX", MakeShared<FRemoteControlProtocolDMX>());
	}
	//~ End IModuleInterface
};

IMPLEMENT_MODULE(FRemoteControlProtocolDMXModule, RemoteControlProtocolDMX);
