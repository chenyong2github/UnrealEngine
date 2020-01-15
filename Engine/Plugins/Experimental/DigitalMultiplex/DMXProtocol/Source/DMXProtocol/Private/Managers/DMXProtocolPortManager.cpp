// Copyright Epic Games, Inc. All Rights Reserved.

#include "Managers/DMXProtocolPortManager.h"
#include "Interfaces/IDMXProtocolPort.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolDevice.h"

void FDMXProtocolPortManager::AddInputPort(TSharedPtr<IDMXProtocolDevice> InDevice, TSharedPtr<IDMXProtocolPort> InInputPort)
{
	checkf(InInputPort.IsValid(), TEXT("IDMXProtocolInputPortPtr not valid. Manager should own only valid pointer."));
	checkf(InDevice.IsValid(), TEXT("InDevice not valid"));

	if (IInputPortsDeviceMap.Contains(InDevice.Get()))
	{
		IPortsMap& InputPortsMap = IInputPortsDeviceMap[InDevice.Get()];
		InputPortsMap.Add(InInputPort->GetPortID(), InInputPort);
	}
	else
	{
		IPortsMap InputPortsMap;
		InputPortsMap.Add(InInputPort->GetPortID(), InInputPort);
		IInputPortsDeviceMap.Add(InDevice.Get(), InputPortsMap);
	}
}

void FDMXProtocolPortManager::AddOutputPort(TSharedPtr<IDMXProtocolDevice> InDevice, TSharedPtr<IDMXProtocolPort> InOutputPort)
{
	checkf(InOutputPort.IsValid(), TEXT("IDMXProtocolOutputPortPtr not valid"));
	checkf(InDevice.IsValid(), TEXT("InDevice not valid"));

	if (IOutputPortsDeviceMap.Contains(InDevice.Get()))
	{
		IPortsMap& OutputPortsMap = IOutputPortsDeviceMap[InDevice.Get()];
		OutputPortsMap.Add(InOutputPort->GetPortID(), InOutputPort);
	}
	else
	{
		IPortsMap OutputPortsMap;
		OutputPortsMap.Add(InOutputPort->GetPortID(), InOutputPort);
		IOutputPortsDeviceMap.Add(InDevice.Get(), OutputPortsMap);
	}
}

void FDMXProtocolPortManager::RemoveInputPort(TSharedPtr<IDMXProtocolDevice> InDevice, TSharedPtr<IDMXProtocolPort> InInputPort)
{
	if (IInputPortsDeviceMap.Contains(InDevice.Get()))
	{
		IPortsMap& InputPortsMap = IInputPortsDeviceMap[InDevice.Get()];
		InputPortsMap.Remove(InInputPort->GetPortID());
	}
}

void FDMXProtocolPortManager::RemoveOutputPort(TSharedPtr<IDMXProtocolDevice> InDevice, TSharedPtr<IDMXProtocolPort> InOutputPort)
{
	if (IOutputPortsDeviceMap.Contains(InDevice.Get()))
	{
		IPortsMap& OutputPortsMap = IOutputPortsDeviceMap[InDevice.Get()];
		OutputPortsMap.Remove(InOutputPort->GetPortID());
	}
}

IPortsMap* FDMXProtocolPortManager::GetInputPortMapByDevice(IDMXProtocolDevice* InDevice)
{
	return IOutputPortsDeviceMap.Find(InDevice);
}

IPortsMap* FDMXProtocolPortManager::GetOutputPortMapByDevice(IDMXProtocolDevice* InDevice)
{
	return IOutputPortsDeviceMap.Find(InDevice);
}

void FDMXProtocolPortManager::RemoveAll()
{
	IInputPortsDeviceMap.Empty();
	IOutputPortsDeviceMap.Empty();
}

IDMXProtocolPort * FDMXProtocolPortManager::GetPortByDeviceAndID(const TSharedPtr<IDMXProtocolDevice>& InDevice, uint8 InPortID, EDMXPortDirection Direction)
{
	IPortsMap* PortsMap = nullptr;
	switch (Direction)
	{
	case DMX_PORT_OUTPUT:
		PortsMap = GetInputPortMapByDevice(InDevice.Get());
		break;
	case DMX_PORT_INPUT:
		PortsMap = GetOutputPortMapByDevice(InDevice.Get());
		break;
	}

	if (PortsMap != nullptr)
	{
		TSharedPtr<IDMXProtocolPort>* Port = PortsMap->Find(InPortID);
		if (Port != nullptr && Port->IsValid())
		{
			if (Port->Get()->GetCachedDevice().HasSameObject(InDevice.Get()))
			{
				return Port->Get();
			}
		}
	}

	return nullptr;
}