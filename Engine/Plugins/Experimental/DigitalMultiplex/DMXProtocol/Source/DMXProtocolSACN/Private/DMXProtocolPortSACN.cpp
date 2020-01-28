// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolPortSACN.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"

FDMXProtocolPortSACN::FDMXProtocolPortSACN(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolDevice> InProtocolDevice, uint8 InPortID, uint16 InUniverseID, EDMXPortDirection InPortDirection)
	: DMXProtocol(InDMXProtocol)
	, ProtocolDevice(InProtocolDevice)
	, PortID(InPortID)
	, UniverseID(InUniverseID)
	, PortDirection(InPortDirection)
{
	checkf(DMXProtocol, TEXT("DMXProtocol pointer is nullptr"));
}

IDMXProtocol * FDMXProtocolPortSACN::GetProtocol() const
{
	return DMXProtocol;
}

uint8 FDMXProtocolPortSACN::GetPortID() const
{
	return PortID;
}

uint8 FDMXProtocolPortSACN::GetPriority() const
{
	return Priority;
}

void FDMXProtocolPortSACN::SetPriotiy(uint8 InPriority)
{
	Priority = InPriority;
}

bool FDMXProtocolPortSACN::IsSupportRDM() const
{
	return bIsRDMSupport;
}

TWeakPtr<IDMXProtocolDevice> FDMXProtocolPortSACN::GetCachedDevice() const
{
	return ProtocolDevice;
}

TWeakPtr<IDMXProtocolUniverse> FDMXProtocolPortSACN::GetCachedUniverse() const
{
	return ProtocolUniverse;
}

void FDMXProtocolPortSACN::SetUniverse(const TSharedPtr<IDMXProtocolUniverse>& InUniverse)
{
	ProtocolUniverse = InUniverse;
}

EDMXPortCapability FDMXProtocolPortSACN::GetPortCapability() const
{
	return DMX_PORT_CAPABILITY_FULL;
}

EDMXPortDirection FDMXProtocolPortSACN::GetPortDirection() const
{
	return PortDirection;
}

bool FDMXProtocolPortSACN::WriteDMX(const TSharedPtr<FDMXBuffer>& DMXBuffer)
{
	return DMXProtocol->SendDMX(UniverseID, PortID, DMXBuffer);
}

bool FDMXProtocolPortSACN::ReadDMX()
{
	// Nothing for now, but there is a reader in Universe
	return false;
}

TSharedPtr<FJsonObject> FDMXProtocolPortSACN::GetSettings() const
{
	return Settings;
}

uint16 FDMXProtocolPortSACN::GetUniverseID() const
{
	return UniverseID;
}

