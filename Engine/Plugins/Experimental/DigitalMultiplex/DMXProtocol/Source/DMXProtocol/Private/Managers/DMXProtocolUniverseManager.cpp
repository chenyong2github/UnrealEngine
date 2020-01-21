// Copyright Epic Games, Inc. All Rights Reserved.

#include "Managers/DMXProtocolUniverseManager.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "Interfaces/IDMXProtocol.h"

void FDMXProtocolUniverseManager::AddUniverse(TSharedPtr<IDMXProtocolPort> InPort, TSharedPtr<IDMXProtocolUniverse> InUniverse)
{
	checkf(InPort.IsValid(), TEXT("IDMXProtocolPortWeakPtr not valid"));
	checkf(InUniverse.IsValid(), TEXT("IDMXProtocolUniversePtr not valid. Manager should own only valid pointer."));
	UniversesMap.Add(InPort.Get(), InUniverse);
	UniversesIDMap.Add(InUniverse->GetUniverseID(), InPort.Get());
}

void FDMXProtocolUniverseManager::RemoveUniverse(TSharedPtr<IDMXProtocolPort> InPort, TSharedPtr<IDMXProtocolUniverse> InUniverse)
{
	UniversesMap.Remove(InPort.Get());
	UniversesIDMap.Remove(InUniverse->GetUniverseID());
}


void FDMXProtocolUniverseManager::RemoveAll()
{
	UniversesMap.Empty();
	UniversesIDMap.Empty();
}

IDMXProtocolUniverse* FDMXProtocolUniverseManager::GetUniverseByPort(IDMXProtocolPort* InPort)
{
	TSharedPtr<IDMXProtocolUniverse>* Port = UniversesMap.Find(InPort);

	if (Port != nullptr)
	{
		return Port->Get();
	}

	return nullptr;
}

IDMXProtocolUniverse* FDMXProtocolUniverseManager::GetUniverseById(uint16 UniverseID)
{
	IDMXProtocolPort** PortAddr = UniversesIDMap.Find(UniverseID);

	if (PortAddr != nullptr)
	{
		TSharedPtr<IDMXProtocolUniverse>* Port = UniversesMap.Find(*PortAddr);

		if (Port != nullptr)
		{
			return Port->Get();
		}
	}
	return nullptr;
}
