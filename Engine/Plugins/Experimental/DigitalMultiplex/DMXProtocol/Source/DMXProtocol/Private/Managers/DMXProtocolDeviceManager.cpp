// Copyright Epic Games, Inc. All Rights Reserved.

#include "Managers/DMXProtocolDeviceManager.h"
#include "Interfaces/IDMXProtocolDevice.h"
#include "Interfaces/IDMXProtocol.h"

void FDMXProtocolDeviceManager::AddDevice(TSharedPtr<IDMXProtocolDevice> InDevice)
{
	checkf(InDevice.IsValid(), TEXT("IDMXProtocolDevicePtr not valid. Manager should own only valid pointer."));
	if (TSharedPtr<IDMXProtocolInterface> Interface = InDevice->GetCachedProtocolInterface().Pin())
	{
		DevicesMap.Add(Interface.Get(), InDevice);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Cached interface pointer not valid"));
	}
}

void FDMXProtocolDeviceManager::RemoveDevice(TSharedPtr<IDMXProtocolDevice> InDevice)
{
	if (TSharedPtr<IDMXProtocolInterface> Interface = InDevice->GetCachedProtocolInterface().Pin())
	{
		DevicesMap.Remove(Interface.Get());
	}
}

void FDMXProtocolDeviceManager::RemoveAll()
{
	DevicesMap.Empty();
}

bool FDMXProtocolDeviceManager::GetDevicesByProtocol(const FName & ProtocolName, IDevicesMap & OutDevicesMap)
{
	bool bIsExists = false;

	for (TMap<IDMXProtocolInterface*, TSharedPtr<IDMXProtocolDevice>>::TConstIterator DeviceIt = DevicesMap.CreateConstIterator(); DeviceIt; ++DeviceIt)
	{
		if (DeviceIt->Value.IsValid() && DeviceIt->Value->GetProtocol())
		{
			if (DeviceIt->Value->GetProtocol()->GetProtocolName() == ProtocolName)
			{
				OutDevicesMap.Add(DeviceIt->Key, DeviceIt->Value);
				bIsExists = true;
			}
		}
	}

	return bIsExists;
}


