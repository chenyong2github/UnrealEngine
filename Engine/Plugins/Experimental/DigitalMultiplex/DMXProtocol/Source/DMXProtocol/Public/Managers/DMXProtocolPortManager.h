// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocol.h"

class DMXPROTOCOL_API FDMXProtocolPortManager
{
public:
	FDMXProtocolPortManager(IDMXProtocol* InDMXProtocol)
		: DMXProtocol(InDMXProtocol)
	{}

public:
	void AddInputPort(TSharedPtr<IDMXProtocolDevice> InDevice, TSharedPtr<IDMXProtocolPort> InInputPort);
	void AddOutputPort(TSharedPtr<IDMXProtocolDevice> InDevice, TSharedPtr<IDMXProtocolPort> InOutputPort);

	void RemoveInputPort(TSharedPtr<IDMXProtocolDevice> InDevice, TSharedPtr<IDMXProtocolPort> InInputPort);
	void RemoveOutputPort(TSharedPtr<IDMXProtocolDevice> InDevice, TSharedPtr<IDMXProtocolPort> InOutputPort);

	IPortsMap* GetInputPortMapByDevice(IDMXProtocolDevice* InDevice);
	IPortsMap* GetOutputPortMapByDevice(IDMXProtocolDevice* InDevice);

	void RemoveAll();

	IDMXProtocolPort* GetPortByDeviceAndID(const TSharedPtr<IDMXProtocolDevice>& InDevice, uint8 InPortID, EDMXPortDirection Direction);

private:
	IPortsDeviceMap IInputPortsDeviceMap;
	IPortsDeviceMap IOutputPortsDeviceMap;
	IDMXProtocol* DMXProtocol;
};
