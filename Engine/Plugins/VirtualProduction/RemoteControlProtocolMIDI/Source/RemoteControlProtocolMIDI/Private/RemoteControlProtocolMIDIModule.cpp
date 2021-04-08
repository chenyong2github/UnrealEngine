// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "IRemoteControlProtocolModule.h"
#include "RemoteControlProtocolMIDI.h"

/**
 * MIDI remote control module
 */
class FRemoteControlProtocolMIDIModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override
	{
		FModuleManager::Get().LoadModuleChecked(TEXT("MIDIDevice"));
		IRemoteControlProtocolModule::Get().AddProtocol("MIDI", MakeShared<FRemoteControlProtocolMIDI>());
	}
	//~ End IModuleInterface
};


IMPLEMENT_MODULE(FRemoteControlProtocolMIDIModule, RemoteControlProtocolMIDI);
