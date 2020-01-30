// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocol.h"

class DMXPROTOCOL_API FDMXProtocolDeviceManager
{
public:
	FDMXProtocolDeviceManager(IDMXProtocol* InDMXProtocol)
		: DMXProtocol(InDMXProtocol)
	{}

public:
	void AddDevice(TSharedPtr<IDMXProtocolDevice> InDevice);
	void RemoveDevice(TSharedPtr<IDMXProtocolDevice> InDevice);
	void RemoveAll();

	bool GetDevicesByProtocol(const FName& ProtocolName, IDevicesMap& OutDevicesMap);

private:
	IDevicesMap DevicesMap;
	IDMXProtocol* DMXProtocol;
};
