// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolUniverseArtNet.h"
#include "Dom/JsonObject.h"
#include "Serialization/ArrayReader.h"

#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolConstants.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolArtNetConstants.h"
#include "DMXProtocolArtNet.h"
#include "DMXStats.h"
#include "DMXProtocolSettings.h"
#include "Packets/DMXProtocolArtNetPackets.h"

// Stats
DECLARE_MEMORY_STAT(TEXT("Art-Net Input And Output Buffer Memory"), STAT_ArtNetInputAndOutputBufferMemory, STATGROUP_DMX);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Art-Net Universes Count"), STAT_ArtNetUniversesCount, STATGROUP_DMX);

//~ Artnet specific constants
static const uint8 HIGH_NIBBLE = 0xF0;
static const uint8 LOW_NIBBLE = 0x0F;
static const uint8 MAX_NET = 0x7f;

FDMXProtocolUniverseArtNet::FDMXProtocolUniverseArtNet(IDMXProtocolPtr InDMXProtocol, const FJsonObject& InSettings)
	: WeakDMXProtocol(InDMXProtocol)
{
	checkf(WeakDMXProtocol.IsValid(), TEXT("DMXProtocol pointer is nullptr"));

	Priority = 100;
	OutputDMXBuffer = MakeShared<FDMXBuffer, ESPMode::ThreadSafe>();
	InputDMXBuffer = MakeShared<FDMXBuffer, ESPMode::ThreadSafe>();

	// Stats
	INC_MEMORY_STAT_BY(STAT_ArtNetInputAndOutputBufferMemory, DMX_UNIVERSE_SIZE * 2);
	INC_DWORD_STAT(STAT_ArtNetUniversesCount);

	UpdateSettings(InSettings);

	/* Bits 15 			Bits 14-8        | Bits 7-4      | Bits 3-0
	* 0 				Net 			 | Sub-Net 		 | Universe
	* 0				    (0b1111111 << 8) | (0b1111 << 4) | (0b111)
	*/
	uint8 Net = (uint8)((UniverseID & 0b0111111100000000) >> 8);
	uint8 Subnet = (uint8)((UniverseID & 0b11110000) >> 4);
	uint8 Universe = (uint8)((UniverseID & 0b1111));
	SetNetAddress(Net);
	SetSubnetAddress(Subnet);
	SetUniverse(Universe);
}


IDMXProtocolPtr FDMXProtocolUniverseArtNet::GetProtocol() const
{
	return WeakDMXProtocol.Pin();
}

FDMXBufferPtr FDMXProtocolUniverseArtNet::GetInputDMXBuffer() const
{
	return InputDMXBuffer;
}

FDMXBufferPtr FDMXProtocolUniverseArtNet::GetOutputDMXBuffer() const
{
	return OutputDMXBuffer;
}

void FDMXProtocolUniverseArtNet::ZeroInputDMXBuffer()
{
	InputDMXBuffer->ZeroDMXBuffer();
}

void FDMXProtocolUniverseArtNet::ZeroOutputDMXBuffer()
{
	OutputDMXBuffer->ZeroDMXBuffer();
}

bool FDMXProtocolUniverseArtNet::SetDMXFragment(const IDMXFragmentMap& DMXFragment)
{
	return OutputDMXBuffer->SetDMXFragment(DMXFragment);
}

uint8 FDMXProtocolUniverseArtNet::GetPriority() const
{
	return Priority;
}

uint32 FDMXProtocolUniverseArtNet::GetUniverseID() const
{
	return UniverseID;
}

TSharedPtr<FJsonObject> FDMXProtocolUniverseArtNet::GetSettings() const
{
	return Settings;
}

void FDMXProtocolUniverseArtNet::UpdateSettings(const FJsonObject& InSettings)
{
	Settings = MakeShared<FJsonObject>(InSettings);
	checkf(Settings->HasField(DMXJsonFieldNames::DMXPortID), TEXT("DMXProtocol PortID is not valid"));
	checkf(Settings->HasField(DMXJsonFieldNames::DMXUniverseID), TEXT("DMXProtocol Universe is not valid"));
	checkf(Settings->HasField(DMXJsonFieldNames::DMXEthernetPort), TEXT("DMXProtocol EthernPort is not valid"));
	checkf(Settings->HasField(DMXJsonFieldNames::DMXIpAddresses), TEXT("DMXProtocol IPAddresses is not valid"));
	PortID = Settings->GetNumberField(DMXJsonFieldNames::DMXPortID);
	UniverseID = Settings->GetNumberField(DMXJsonFieldNames::DMXUniverseID);
	EthernetPort = Settings->GetNumberField(DMXJsonFieldNames::DMXEthernetPort);
	IpAddresses.Empty();
	for (TSharedPtr<FJsonValue> JsonIpAddress : Settings->GetArrayField(DMXJsonFieldNames::DMXIpAddresses))
	{
		uint64 IpAddress = 0;
		const bool bValid = JsonIpAddress->TryGetNumber(IpAddress);
		checkf(bValid, TEXT("DMXProtocol IPAddresses content is not valid"));
		IpAddresses.Add(IpAddress);
	}
}

bool FDMXProtocolUniverseArtNet::IsSupportRDM() const
{
	return true;
}

void FDMXProtocolUniverseArtNet::HandleReplyPacket(const FArrayReaderPtr& Buffer)
{	
	bool bSetDataSuccessful = false;
	if (GetInputDMXBuffer().IsValid())
	{
		// ArtNet DMX packet
		FDMXProtocolArtNetDMXPacket ArtNetDMXPacket;
		*Buffer << ArtNetDMXPacket;

		GetInputDMXBuffer()->AccessDMXData([this, &ArtNetDMXPacket, &bSetDataSuccessful](TArray<uint8>& InData)
		{
			// Make sure we copy same amount of data
			if (InData.Num() == ARTNET_DMX_LENGTH)
			{
				GetInputDMXBuffer()->SetDMXBuffer(ArtNetDMXPacket.Data, ARTNET_DMX_LENGTH);
				GetProtocol()->GetOnUniverseInputBufferUpdated().Broadcast(GetProtocol()->GetProtocolName(), ArtNetDMXPacket.Universe, InData);
				bSetDataSuccessful = true;
			}
			else
			{
				UE_LOG_DMXPROTOCOL(Error, TEXT("%s: Size of incoming DMX buffer is wrong. Expected size: %d. Current: %d")
					, TEXT("NETWORK ERROR Art-Net:")
					, ARTNET_DMX_LENGTH
					, InData.Num());
				bSetDataSuccessful = false;
			}
		});

		if (bSetDataSuccessful)
		{
			GetProtocol()->GetOnPacketReceived().Broadcast(GetProtocol()->GetProtocolName(), UniverseID, *Buffer);
		}
	}
}

uint8 FDMXProtocolUniverseArtNet::GetPortID()
{
	return PortID;
}

bool FDMXProtocolUniverseArtNet::SetNetAddress(uint8 InNet)
{
	if (InNet & 0x80)
	{
		UE_LOG_DMXPROTOCOL(Verbose, TEXT("Art-Net net address > 127, truncating!"));
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

bool FDMXProtocolUniverseArtNet::SetSubnetAddress(uint8 InSubnetAddress)
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

bool FDMXProtocolUniverseArtNet::SetUniverse(uint8 InUniverse)
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

uint8 FDMXProtocolUniverseArtNet::GetPortAddress() const
{
	return PortAddress;
}

uint8 FDMXProtocolUniverseArtNet::GetNetAddress() const
{
	return NetAddress;
}

uint8 FDMXProtocolUniverseArtNet::GetSubnetAddress() const
{
	return SubnetAddress;
}

uint8 FDMXProtocolUniverseArtNet::GetUniverseAddress() const
{
	return UniverseAddress;
}

const TArray<FRDMUID>& FDMXProtocolUniverseArtNet::GetTODUIDs() const
{
	return TODUIDs;
}

void FDMXProtocolUniverseArtNet::AddTODUID(const FRDMUID& InUID)
{
	TODUIDs.Add(InUID);
}

