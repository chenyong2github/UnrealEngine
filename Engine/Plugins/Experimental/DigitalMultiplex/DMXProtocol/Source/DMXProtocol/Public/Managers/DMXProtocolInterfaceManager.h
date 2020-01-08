// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocol.h"

/**
 * There is 2 different types
 * If it ethernet protocol device it the IP adderess of network card. you could specify IP address  of network card manually
 * This is helpful if PC has multiple network interfaces
 * It is USB device this is serial port
 */
class DMXPROTOCOL_API FDMXProtocolInterfaceManager
{
public:
	FDMXProtocolInterfaceManager(IDMXProtocol* InDMXProtocol)
		: DMXProtocol(InDMXProtocol)
	{}

public:
	void AddInterface(int32 InInterfaceID, TSharedPtr<IDMXProtocolInterface> InInterface);
	void RemoveInterface(int32 InInterfaceID);
	void RemoveAll();

private:
	IInterfacesMap InterfacesMap;
	IDMXProtocol* DMXProtocol;
};
