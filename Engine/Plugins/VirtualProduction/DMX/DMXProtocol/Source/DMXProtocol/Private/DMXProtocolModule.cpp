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


const int32 FDMXProtocolModule::NumProtocols = 2;

FDMXProtocolModule::FDMXProtocolModule()
	: NumRegisteredProtocols(0)
{}

void FDMXProtocolModule::RegisterProtocol(const FName& FactoryName, IDMXProtocolFactory* Factory)
{
	if (!DMXProtocolFactories.Contains(FactoryName))
	{
		// Increment registred protocol counter
		NumRegisteredProtocols++;

		// If this check is, please change the NumProtocols variable to match the number of protocol implementations
		check(NumRegisteredProtocols <= NumProtocols);

		// Add a factory for the protocol
		DMXProtocolFactories.Add(FactoryName, Factory);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Verbose, TEXT("Trying to add existing protocol %s"), *FactoryName.ToString());
	}

	// Broadcast protocol registered when all protocols are registered
	if (NumRegisteredProtocols == NumProtocols)
	{
		// Create instances of all protocol implementations
		for (const TTuple<FName, IDMXProtocolFactory*>& ProtocolNameToFactoryPair : DMXProtocolFactories)
		{
			IDMXProtocolPtr NewProtocol = ProtocolNameToFactoryPair.Value->CreateProtocol(ProtocolNameToFactoryPair.Key);
			if (NewProtocol.IsValid())
			{
				DMXProtocols.Add(ProtocolNameToFactoryPair.Key, NewProtocol);

				UE_LOG_DMXPROTOCOL(Log, TEXT("Creating protocol instance for: %s"), *ProtocolNameToFactoryPair.Key.ToString());
			}
			else
			{
				UE_LOG_DMXPROTOCOL(Verbose, TEXT("Unable to create Protocol %s"), *ProtocolNameToFactoryPair.Key.ToString());
			}
		}

		OnProtocolsRegistered.Broadcast();
	}
}

void FDMXProtocolModule::UnregisterProtocol(const FName& FactoryName)
{
	if (DMXProtocolFactories.Contains(FactoryName))
	{
		// Decrement the registered protocol counter
		NumRegisteredProtocols--;

		// Destroy the factory and shut down the protocol
		DMXProtocolFactories.Remove(FactoryName);
		ShutdownDMXProtocol(FactoryName);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Verbose, TEXT("Trying to remove unexisting protocol %s"), *FactoryName.ToString());
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
	// Deffer initialization to after all protocols begin registered
	OnProtocolsRegistered.AddRaw(this, &FDMXProtocolModule::HandleProtocolsRegistered);
}

void FDMXProtocolModule::ShutdownModule()
{
	OnProtocolsRegistered.RemoveAll(this);

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

void FDMXProtocolModule::HandleProtocolsRegistered()
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

	// Start the port manager after settings are registered, so it can create its default ports from that
	FDMXPortManager::StartupManager();
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
