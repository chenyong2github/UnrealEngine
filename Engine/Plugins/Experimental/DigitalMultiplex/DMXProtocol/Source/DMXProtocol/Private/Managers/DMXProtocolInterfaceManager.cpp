// Copyright Epic Games, Inc. All Rights Reserved.

#include "Managers/DMXProtocolInterfaceManager.h"
#include "Interfaces/IDMXProtocolInterface.h"
#include "Interfaces/IDMXProtocol.h"

void FDMXProtocolInterfaceManager::AddInterface(int32 InInterfaceID, TSharedPtr<IDMXProtocolInterface> InInterface)
{
	checkf(InInterface.IsValid(), TEXT("IDMXProtocolInterfacePtr not valid. Manager should own only valid pointer."));
	InterfacesMap.Add(InInterfaceID, InInterface);
}

void FDMXProtocolInterfaceManager::RemoveInterface(int32 InInterfaceID)
{
	InterfacesMap.Remove(InInterfaceID);
}

void FDMXProtocolInterfaceManager::RemoveAll()
{
	InterfacesMap.Empty();
}
