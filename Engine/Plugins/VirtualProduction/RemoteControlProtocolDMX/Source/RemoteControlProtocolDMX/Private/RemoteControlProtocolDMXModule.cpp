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
		const IRemoteControlProtocolModule& RemoteControlProtocolModule = IRemoteControlProtocolModule::Get();
		if (!RemoteControlProtocolModule.IsRCProtocolsDisable())
		{
			IRemoteControlProtocolModule::Get().AddProtocol(FRemoteControlProtocolDMX::ProtocolName, MakeShared<FRemoteControlProtocolDMX>());
		}
	}
	//~ End IModuleInterface
};

IMPLEMENT_MODULE(FRemoteControlProtocolDMXModule, RemoteControlProtocolDMX);
