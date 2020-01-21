// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolModule.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolFactory.h"

IMPLEMENT_MODULE( FDMXProtocolModule, DMXProtocol );

#define LOCTEXT_NAMESPACE "DMXProtocolModule"

const TCHAR* FDMXProtocolModule::BaseModuleName = TEXT("DMXProtocol");

void FDMXProtocolModule::ShutdownModule()
{
	ShutdownAllDMXProtocols();
}

static FName GetProtocolModuleName(const FString& ProtocolName)
{
	FName ModuleName;
	if (!ProtocolName.StartsWith(FDMXProtocolModule::BaseModuleName, ESearchCase::CaseSensitive))
	{
		ModuleName = FName(*(FDMXProtocolModule::BaseModuleName + ProtocolName));
	}
	else
	{
		ModuleName = FName(*ProtocolName);
	}

	return ModuleName;
}

static IModuleInterface* GetProtocolModule(const FString& SubsystemName)
{
	const FName ModuleName = GetProtocolModuleName(SubsystemName);
	FModuleManager& ModuleManager = FModuleManager::Get();
	check(ModuleManager.IsModuleLoaded(ModuleName));
	return ModuleManager.GetModule(ModuleName);
}

void FDMXProtocolModule::RegisterProtocol(const FName& FactoryName, IDMXProtocolFactory* Factory)
{
	if (!DMXFactories.Contains(FactoryName))
	{
		DMXFactories.Add(FactoryName, Factory);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Trying to add existing protocol %s"), *FactoryName.ToString());
	}
}

void FDMXProtocolModule::UnregisterProtocol(const FName& FactoryName)
{
	if (DMXFactories.Contains(FactoryName))
	{
		DMXFactories.Remove(FactoryName);
		ShutdownDMXProtocol(FactoryName);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Trying to remove unexisting protocol %s"), *FactoryName.ToString());
	}
}

FDMXProtocolModule& FDMXProtocolModule::Get()
{
	return FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");
}


IDMXProtocol* FDMXProtocolModule::GetProtocol(const FName InProtocolName)
{
	TSharedPtr<IDMXProtocol>* DMXProtocolPtr = nullptr;
	if (!InProtocolName.IsNone())
	{
		DMXProtocolPtr = DMXProtocols.Find(InProtocolName);
		if (DMXProtocolPtr == nullptr)
		{
			IDMXProtocolFactory** DMXProtocolFactory = DMXFactories.Find(InProtocolName);

			if (DMXProtocolFactory != nullptr)
			{
				UE_LOG_DMXPROTOCOL(Log, TEXT("Creating protocol instance for: %s"), *InProtocolName.ToString());

				TSharedPtr<IDMXProtocol> NewProtocol = (*DMXProtocolFactory)->CreateProtocol(InProtocolName);
				if (NewProtocol.IsValid())
				{
					DMXProtocols.Add(InProtocolName, NewProtocol);
					DMXProtocolPtr = DMXProtocols.Find(InProtocolName);
				}
				else
				{
					bool* bNotedPreviously = DMXProtocolFailureNotes.Find(InProtocolName);
					if (!bNotedPreviously || !(*bNotedPreviously))
					{
						UE_LOG_DMXPROTOCOL(Warning, TEXT("Unable to create Protocol %s"), *InProtocolName.ToString());
						DMXProtocolFailureNotes.Add(InProtocolName, true);
					}
				}
			}
		}
	}

	return (DMXProtocolPtr == nullptr) ? nullptr : (*DMXProtocolPtr).Get();
}

const TMap<FName, IDMXProtocolFactory*>& FDMXProtocolModule::GetProtocolFactories() const
{
	return DMXFactories;
}

void FDMXProtocolModule::ShutdownDMXProtocol(const FName& ProtocolName)
{
	if (!ProtocolName.IsNone())
	{
		TSharedPtr<IDMXProtocol> DMXProtocol;
		DMXProtocols.RemoveAndCopyValue(ProtocolName, DMXProtocol);
		if (DMXProtocol.IsValid())
		{
			DMXProtocol->Shutdown();
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Warning, TEXT("DMXProtocol instance %s not found, unable to destroy."), *ProtocolName.ToString());
		}
	}
}

void FDMXProtocolModule::ShutdownAllDMXProtocols()
{
	for (TMap<FName, TSharedPtr<IDMXProtocol>>::TIterator It = DMXProtocols.CreateIterator(); It; ++It)
	{
		It->Value->Shutdown();
	}
}

#undef LOCTEXT_NAMESPACE
