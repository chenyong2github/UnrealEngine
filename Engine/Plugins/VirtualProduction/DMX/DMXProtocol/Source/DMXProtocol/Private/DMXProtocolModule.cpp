// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolModule.h"

#include "DMXProtocolLog.h"
#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolFactory.h"
#include "IO/DMXPortManager.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#endif // WITH_EDITOR


IMPLEMENT_MODULE( FDMXProtocolModule, DMXProtocol );

#define LOCTEXT_NAMESPACE "DMXProtocolModule"

void FDMXProtocolModule::RegisterProtocol(const FName& ProtocolName, IDMXProtocolFactory* Factory)
{
	if (!DMXProtocolFactories.Contains(ProtocolName))
	{
		// Add a factory for the protocol
		DMXProtocolFactories.Add(ProtocolName, Factory);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Verbose, TEXT("Trying to add existing protocol %s"), *ProtocolName.ToString());
	}

	IDMXProtocolPtr NewProtocol = Factory->CreateProtocol(ProtocolName);
	if (NewProtocol.IsValid())
	{
		DMXProtocols.Add(ProtocolName, NewProtocol);

		UE_LOG_DMXPROTOCOL(Log, TEXT("Creating protocol instance for: %s"), *ProtocolName.ToString());
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Verbose, TEXT("Unable to create Protocol %s"), *ProtocolName.ToString());
	}

	OnProtocolRegistered.Broadcast();
}

void FDMXProtocolModule::UnregisterProtocol(const FName& ProtocolName)
{
	if (DMXProtocolFactories.Contains(ProtocolName))
	{
		// Destroy the factory and shut down the protocol
		DMXProtocolFactories.Remove(ProtocolName);
		ShutdownDMXProtocol(ProtocolName);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Verbose, TEXT("Trying to remove unexisting protocol %s"), *ProtocolName.ToString());
	}
}

FDMXProtocolModule& FDMXProtocolModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");
}

IDMXProtocolPtr FDMXProtocolModule::GetProtocol(const FName InProtocolName)
{
	IDMXProtocolPtr* ProtocolPtr = DMXProtocols.Find(InProtocolName);

	return ProtocolPtr ? *ProtocolPtr : nullptr;
}

const TMap<FName, IDMXProtocolFactory*>& FDMXProtocolModule::GetProtocolFactories() const
{
	return DMXProtocolFactories;
}

const TMap<FName, IDMXProtocolPtr>& FDMXProtocolModule::GetProtocols() const
{
	return DMXProtocols;
}

void FDMXProtocolModule::StartupModule()
{
#if WITH_EDITOR
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	// Register DMX Protocol global settings
	if (SettingsModule != nullptr)
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "DMX Plugin",
			LOCTEXT("ProjectSettings_Label", "DMX Plugin"),
			LOCTEXT("ProjectSettings_Description", "Configure DMX plugin global settings"),
			GetMutableDefault<UDMXProtocolSettings>()
		);
	}
#endif // WITH_EDITOR

	FDMXPortManager::StartupManager();
}

void FDMXProtocolModule::ShutdownModule()
{
	FDMXPortManager::ShutdownManager();

	// Now Shutdown the protocols
	ShutdownAllDMXProtocols();

#if WITH_EDITOR
	// Unregister DMX Protocol global settings
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "DMX Protocol");
	}
#endif // WITH_EDITOR
}

void FDMXProtocolModule::ShutdownDMXProtocol(const FName& ProtocolName)
{
	if (!ProtocolName.IsNone())
	{
		IDMXProtocolPtr DMXProtocol;
		DMXProtocols.RemoveAndCopyValue(ProtocolName, DMXProtocol);
		if (DMXProtocol.IsValid())
		{
			DMXProtocol->Shutdown();
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Verbose, TEXT("DMXProtocol instance %s not found, unable to destroy."), *ProtocolName.ToString());
		}
	}
}

void FDMXProtocolModule::ShutdownAllDMXProtocols()
{
	for (TMap<FName, IDMXProtocolPtr>::TIterator It = DMXProtocols.CreateIterator(); It; ++It)
	{
		It->Value->Shutdown();
	}
}

#undef LOCTEXT_NAMESPACE
