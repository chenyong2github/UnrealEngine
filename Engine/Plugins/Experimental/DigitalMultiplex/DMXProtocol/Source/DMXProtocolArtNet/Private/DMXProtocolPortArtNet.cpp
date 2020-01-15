// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolPortArtNet.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"

#include "Dom/JsonObject.h"

//~ Artnet specific port constants
static const uint8 HIGH_NIBBLE = 0xF0;
static const uint8 LOW_NIBBLE = 0x0F;
static const uint8 MAX_NET = 0x7f;

FDMXProtocolPortArtNet::FDMXProtocolPortArtNet(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolDevice> InProtocolDevice, const FJsonObject& InSettings, uint8 InPortID, EDMXPortDirection InPortDirection)
	: DMXProtocol(InDMXProtocol)
	, ProtocolDevice(InProtocolDevice)
	, PortID(InPortID)
	, PortAddress(0)
	, NetAddress(0)
	, SubnetAddress(0)
	, UniverseAddress(0)
	, bIsRDMSupport(true)
	, PortDirection(InPortDirection)
{
	checkf(DMXProtocol, TEXT("DMXProtocol pointer is nullptr"));

	Settings = MakeShared<FJsonObject>(InSettings);

	// Try to set Subnet and Universe
	/* Bits 15 			Bits 14-8        | Bits 7-4      | Bits 3-0
	* 0 				Net 			 | Sub-Net 		 | Universe
	* 0				    (0b1111111 << 8) | (0b1111 << 4) | (0b1111)
	*/
	uint8 Net = Settings->GetNumberField(TEXT("Net"));
	uint8 Subnet = Settings->GetNumberField(TEXT("Subnet"));
	uint8 Universe = Settings->GetNumberField(TEXT("Universe"));
	SetNetAddress(Net);
	SetSubnetAddress(Subnet);
	SetUniverse(Universe);

	UniverseID = (Net << 8) | (Subnet << 4) | Universe;
}

IDMXProtocol * FDMXProtocolPortArtNet::GetProtocol() const
{
	return DMXProtocol;
}

uint8 FDMXProtocolPortArtNet::GetPortID() const
{
	return PortID;
}

uint8 FDMXProtocolPortArtNet::GetPriority() const
{
	return Priority;
}

EDMXPortCapability FDMXProtocolPortArtNet::GetPortCapability() const
{
	return DMX_PORT_CAPABILITY_FULL;
}

EDMXPortDirection FDMXProtocolPortArtNet::GetPortDirection() const
{
	return PortDirection;
}

bool FDMXProtocolPortArtNet::WriteDMX(const TSharedPtr<FDMXBuffer>& DMXBuffer)
{
	return DMXProtocol->SendDMX(UniverseID, PortID, DMXBuffer);
}

void FDMXProtocolPortArtNet::SetPriotiy(uint8 InPriority)
{
	Priority = InPriority;
}

bool FDMXProtocolPortArtNet::IsSupportRDM() const
{
	return bIsRDMSupport;
}

TWeakPtr<IDMXProtocolDevice> FDMXProtocolPortArtNet::GetCachedDevice() const
{
	return ProtocolDevice;
}

TWeakPtr<IDMXProtocolUniverse> FDMXProtocolPortArtNet::GetCachedUniverse() const
{
	return ProtocolUniverse;
}

void FDMXProtocolPortArtNet::SetUniverse(const TSharedPtr<IDMXProtocolUniverse>& InUniverse)
{
	ProtocolUniverse = InUniverse;
}

bool FDMXProtocolPortArtNet::ReadDMX()
{
	return false;
}

TSharedPtr<FJsonObject> FDMXProtocolPortArtNet::GetSettings() const
{
	return Settings;
}

uint16 FDMXProtocolPortArtNet::GetUniverseID() const
{
	return UniverseID;
}

bool FDMXProtocolPortArtNet::SetNetAddress(uint8 InNet)
{
	if (InNet & 0x80) 
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Art-Net net address > 127, truncating!"));
		NetAddress = InNet & MAX_NET;
		return true;
	}
	if (NetAddress == InNet)
	{
		return false;
	}

	NetAddress = InNet;
	return true;
}

bool FDMXProtocolPortArtNet::SetSubnetAddress(uint8 InSubnetAddress)
{
	// Bits 7-4
	// Shift for 4 bytes
	SubnetAddress = InSubnetAddress << 4;
	if (SubnetAddress == (PortAddress & HIGH_NIBBLE)) 
	{
		return false;
	}

	PortAddress = SubnetAddress | (PortAddress & LOW_NIBBLE);

	return true;
}

bool FDMXProtocolPortArtNet::SetUniverse(uint8 InUniverse)
{
	// Bits 1-3
	UniverseAddress = InUniverse & LOW_NIBBLE;
	if ((PortAddress & LOW_NIBBLE) == UniverseAddress) 
	{
		return false;
	}

	PortAddress = ((PortAddress & HIGH_NIBBLE) | UniverseAddress);

	return true;
}

uint8 FDMXProtocolPortArtNet::GetPortAddress() const
{
	return PortAddress;
}

uint8 FDMXProtocolPortArtNet::GetNetAddress() const
{
	return NetAddress;
}

uint8 FDMXProtocolPortArtNet::GetSubnetAddress() const
{
	return SubnetAddress;
}

uint8 FDMXProtocolPortArtNet::GetUniverseAddress() const
{
	return UniverseAddress;
}

const TArray<FRDMUID>& FDMXProtocolPortArtNet::GetTODUIDs() const
{
	return TODUIDs;
}

void FDMXProtocolPortArtNet::AddTODUID(const FRDMUID & InUID)
{
	TODUIDs.Add(InUID);
}
