// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACNModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolSACN.h"
#include "DMXProtocolTypes.h"
#include "Dom/JsonObject.h"

#include "Managers/DMXProtocolDeviceManager.h"
#include "Managers/DMXProtocolInterfaceManager.h"
#include "Managers/DMXProtocolPortManager.h"
#include "Managers/DMXProtocolUniverseManager.h"

#include "DMXProtocolInterfaceSACN.h"
#include "DMXProtocolDeviceSACN.h"
#include "DMXProtocolUniverseSACN.h"
#include "DMXProtocolPortSACN.h"
#include "DMXProtocolSACNConstants.h"

IMPLEMENT_MODULE(FDMXProtocolSACNModule, DMXProtocolSACN);

const FName FDMXProtocolSACNModule::NAME_SACN = FName(TEXT("sACN"));

FAutoConsoleCommand FDMXProtocolSACNModule::SendDMXCommand(
	TEXT("DMX.SACN.SendDMX"),
	TEXT("Command for sending DMX through SACN Protocol. DMX.SACN.SendDMX [UniverseID] Channel:Value Channel:Value Channel:Value n\t DMX.SACN.SendDMX 17 10:6 11:7 12:8 13:9 n\t It will send channels values to the DMX to Universe 17"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FDMXProtocolSACNModule::SendDMXCommandHandler)
);

TSharedPtr<IDMXProtocol> FDMXProtocolFactorySACN::CreateProtocol(const FName & ProtocolName)
{
	FJsonObject ProtocolSettings;
	TSharedPtr<IDMXProtocol> ProtocolSACNPtr = MakeShared<FDMXProtocolSACN>(ProtocolName, ProtocolSettings);
	if (ProtocolSACNPtr->IsEnabled())
	{
		if (!ProtocolSACNPtr->Init())
		{
			UE_LOG_DMXPROTOCOL(Warning, TEXT("SACN failed to initialize!"));
			ProtocolSACNPtr->Shutdown();
			ProtocolSACNPtr = nullptr;
		}
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("SACN disabled!"));
		ProtocolSACNPtr->Shutdown();
		ProtocolSACNPtr = nullptr;
	}

	return ProtocolSACNPtr;
}

void FDMXProtocolSACNModule::StartupModule()
{
	FactorySACN = MakeUnique<FDMXProtocolFactorySACN>();

	// Create and register our singleton factory with the main online subsystem for easy access
	FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");
	DMXProtocolModule.RegisterProtocol(NAME_SACN, FactorySACN.Get());
}

void FDMXProtocolSACNModule::ShutdownModule()
{
	// Unregister and destroy protocol
	FDMXProtocolModule* DMXProtocolModule = FModuleManager::GetModulePtr<FDMXProtocolModule>("DMXProtocol");
	if (DMXProtocolModule != nullptr)
	{
		DMXProtocolModule->UnregisterProtocol(NAME_SACN);
	}

	FactorySACN.Release();
}

FDMXProtocolSACNModule& FDMXProtocolSACNModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXProtocolSACNModule>("DMXProtocolSACN");
}

void FDMXProtocolSACNModule::SendDMXCommandHandler(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Not enough arguments. It won't be sent\n"
			"Command structure is DMX.SACN.SendDMX [UniverseID] Channel:Value Channel:Value Channel:Value\n"
			"For example: DMX.SACN.SendDMX 17 10:6 11:7 12:8 13:9"));
		return;
	}

	uint32 UniverseID = 0;
	LexTryParseString<uint32>(UniverseID, *Args[0]);
	if (UniverseID > ACN_MAX_UNIVERSES)
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("The UniverseID is bigger then the max universe value. It won't be sent. It won't be sent\n"
			"For example: DMX.SACN.SendDMX 17 10:6 11:7 12:8 13:9\n"
			"Where Universe %d should be less then %d"), UniverseID, ACN_MAX_UNIVERSES);
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
				"For example: DMX.SACN.SendDMX 17 10:6 11:7 12:8 13:9\n"
				"Where channel %d should be less then %d"), Key, DMX_UNIVERSE_SIZE);
			return;
		}
		LexTryParseString<uint32>(Value, *ValueStr);
		if (Value > DMX_MAX_CHANNEL_VALUE)
		{
			UE_LOG_DMXPROTOCOL(Warning, TEXT("The input value is bigger then the universe size. It won't be sent\n"
				"For example: DMX.SACN.SendDMX 17 10:6 11:7 12:8 13:9\n"
				"Where value %d should be less then %d"), Value, DMX_MAX_CHANNEL_VALUE);
			return;
		}
		DMXFragment.Add(Key, Value);
	}

	FDMXProtocolSACN* DMXProtocol = IDMXProtocol::Get<FDMXProtocolSACN>(FDMXProtocolSACNModule::NAME_SACN);
	DMXProtocol->SetDMXFragment(UniverseID, DMXFragment, true);
}
