// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolArtNetModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Dom/JsonObject.h"

#include "DMXProtocolArtNet.h"
#include "DMXProtocolTypes.h"


IMPLEMENT_MODULE(FDMXProtocolArtNetModule, DMXProtocolArtNet);

const FName FDMXProtocolArtNetModule::NAME_Artnet = FName(TEXT("Art-Net"));

FAutoConsoleCommand FDMXProtocolArtNetModule::SendDMXCommand(
	TEXT("DMX.ArtNet.SendDMX"),
	TEXT("Command for sending DMX through ArtNet Protocol. DMX.ArtNet.SendDMX [UniverseID] Channel:Value Channel:Value Channel:Value n\t DMX.ArtNet.SendDMX 17 10:6 11:7 12:8 13:9 n\t It will send channels values to the DMX to Universe 17"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FDMXProtocolArtNetModule::SendDMXCommandHandler)
);

TSharedPtr<IDMXProtocol> FDMXProtocolFactoryArtNet::CreateProtocol(const FName & ProtocolName)
{
	FJsonObject ProtocolSettings;
	TSharedPtr<IDMXProtocol> ProtocolArtNetPtr = MakeShared<FDMXProtocolArtNet>(ProtocolName, ProtocolSettings);
	if (ProtocolArtNetPtr->IsEnabled())
	{
		if (!ProtocolArtNetPtr->Init())
		{
			UE_LOG_DMXPROTOCOL(Warning, TEXT("ArtNet failed to initialize!"));
			ProtocolArtNetPtr->Shutdown();
			ProtocolArtNetPtr = nullptr;
		}
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("ArtNet disabled!"));
		ProtocolArtNetPtr->Shutdown();
		ProtocolArtNetPtr = nullptr;
	}

	return ProtocolArtNetPtr;
}

void FDMXProtocolArtNetModule::StartupModule()
{
	FactoryArtNet = MakeUnique<FDMXProtocolFactoryArtNet>();

	// Create and register our singleton factory with the main online subsystem for easy access
	FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");
	DMXProtocolModule.RegisterProtocol(NAME_Artnet, FactoryArtNet.Get());
}

void FDMXProtocolArtNetModule::ShutdownModule()
{
	// Unregister and destroy protocol
	FDMXProtocolModule* DMXProtocolModule = FModuleManager::GetModulePtr<FDMXProtocolModule>("DMXProtocol");
	if (DMXProtocolModule != nullptr)
	{
		DMXProtocolModule->UnregisterProtocol(NAME_Artnet);
	}
	
	FactoryArtNet.Release();
}

FDMXProtocolArtNetModule& FDMXProtocolArtNetModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXProtocolArtNetModule>("DMXProtocolArtNet");
}

void FDMXProtocolArtNetModule::SendDMXCommandHandler(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Not enough arguments. It won't be sent\n"
										"Command structure is DMX.ArtNet.SendDMX [UniverseID] Channel:Value Channel:Value Channel:Value\n"
										"For example: DMX.ArtNet.SendDMX 17 10:6 11:7 12:8 13:9"));
		return;
	}

	uint32 UniverseID = 0;
	LexTryParseString<uint32>(UniverseID, *Args[0]);
	if (UniverseID > ARTNET_MAX_UNIVERSES)
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("The UniverseID is bigger then the max universe value. It won't be sent\n"
			"For example: DMX.ArtNet.SendDMX 17 10:6 11:7 12:8 13:9\n"
			"Where Universe %d should be less then %d"), UniverseID, ARTNET_MAX_UNIVERSES);
		return;
	}

	IDMXFragmentMap DMXFragment;
	for (int32 i = 1; i < Args.Num(); i++)
	{
		FString ChannelAndValue = Args[i];

		FString KeyStr;
		FString ValueStr;
		ChannelAndValue.Split(TEXT(":"), &KeyStr, &ValueStr);

		uint32 Key = 0;
		uint32 Value = 0;
		LexTryParseString<uint32>(Key, *KeyStr);
		if (Key > DMX_UNIVERSE_SIZE)
		{
			UE_LOG_DMXPROTOCOL(Warning, TEXT("The input channel is bigger then the universe size. It won't be sent\n"
				"For example: DMX.ArtNet.SendDMX 17 10:6 11:7 12:8 13:9\n"
				"Where channel %d should be less then %d"), Key, DMX_UNIVERSE_SIZE);
			return;
		}
		LexTryParseString<uint32>(Value, *ValueStr);
		if (Value > DMX_MAX_CHANNEL_VALUE)
		{
			UE_LOG_DMXPROTOCOL(Warning, TEXT("The input value is bigger then the universe size. It won't be sent\n"
				"For example: DMX.ArtNet.SendDMX 17 10:6 11:7 12:8 13:9\n"
				"Where value %d should be less then %d"), Value, DMX_MAX_CHANNEL_VALUE);
			return;
		}
		DMXFragment.Add(Key, Value);
	}

	FDMXProtocolArtNet* DMXProtocol = IDMXProtocol::Get<FDMXProtocolArtNet>(FDMXProtocolArtNetModule::NAME_Artnet);
	DMXProtocol->SetDMXFragment(UniverseID, DMXFragment, true);
}
