// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACN.h"
#include "DMXProtocolTransportSACN.h"
#include "Common/UdpSocketBuilder.h"

#include "Packets/DMXProtocolE131PDUPacket.h"
#include "DMXProtocolPackager.h"
#include "DMXProtocolSettings.h"

#include "Serialization/BufferArchive.h"

TSharedPtr<IDMXProtocolSender> FDMXProtocolSACN::GetSenderInterface() const
{
	return SACNSender;
}

bool FDMXProtocolSACN::Init()
{	
	//Socket;
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	bool bIsValid = false;

	// Create broadcast address
	FIPv4Endpoint UnicastEndpoint = FIPv4Endpoint::Any;

	// Create sender socket
	SenderSocket = FUdpSocketBuilder(TEXT("UDPSACNSenderSocket"))
		.AsNonBlocking()
#if PLATFORM_WINDOWS
		.BoundToAddress(UnicastEndpoint.Address)
#endif
		.AsReusable();

	if (SenderSocket == nullptr)
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("ERROR create SenderSocket"));
		return false;
	}

	SACNSender = MakeShared<FDMXProtocolSenderSACN>(*SenderSocket, this);

	return true;
}

bool FDMXProtocolSACN::Shutdown()
{
	SACNSender.Reset();

	//Clear all sockets!
	if (SenderSocket)
	{
		SenderSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SenderSocket);
	}

	return false;
}

bool FDMXProtocolSACN::IsEnabled() const
{
	return true;
}

FName FDMXProtocolSACN::GetProtocolName() const
{
	return FDMXProtocolSACNModule::NAME_SACN;
}

void FDMXProtocolSACN::Reload()
{
}

bool FDMXProtocolSACN::Tick(float DeltaTime)
{
	return true;
}

bool FDMXProtocolSACN::SendDiscovery(const TArray<uint16>& Universes)
{
	// Init Packager
	FDMXProtocolPackager Packager;

	FDMXProtocolE131RootLayerPacket RootLayer;
	RootLayer.Flags = 0x72; // Const for now
	RootLayer.Length = 0x6e; // Const for now
	FGuid Guid = FGuid::NewGuid();
	FMemory::Memcpy(RootLayer.CID, &Guid, ACN_CIDBYTES);

	FDMXProtocolUDPE131FramingLayerPacket FramingLayer;
	FDMXProtocolUDPE131DiscoveryLayerPacket DiscoveryLayer;
	if (Universes.Num() == ACN_DMX_SIZE)
	{
		FMemory::Memcpy(DiscoveryLayer.Universes, Universes.GetData(), ACN_DMX_SIZE);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Size of outcoming sACN DMX discovery buffer is wrong"));
	}

	Packager.AddToPackage(&RootLayer);
	Packager.AddToPackage(&FramingLayer);
	Packager.AddToPackage(&DiscoveryLayer);

	// Sending
	FJsonObject PacketSettings;
	PacketSettings.SetNumberField("UniverseID", ACN_MAX_UNIVERSES);
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(PacketSettings, Packager.GetBuffer());
	GetSenderInterface()->EnqueueOutboundPackage(Packet);

	return true;
}

bool FDMXProtocolSACN::SendDMX(uint16 UniverseID, uint8 PortID, const TSharedPtr<FDMXBuffer>& DMXBuffer) const
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// SACN PDU packets
	FDMXProtocolE131RootLayerPacket RootLayer;;
	RootLayer.PreambleSize = ACN_RLP_PREAMBLE_SIZE;
	RootLayer.PostambleSize = 0x0000;
	RootLayer.Flags = 0x72; // Const for now
	RootLayer.Length = 0x6e; // Const for now
	RootLayer.Vector = 4; // Const for now
	FGuid Guid = FGuid::NewGuid();
	FMemory::Memcpy(RootLayer.CID, &Guid, ACN_CIDBYTES);

	FDMXProtocolE131FramingLayerPacket FramingLayer;
	FramingLayer.Flags = 0x72; // Const for now
	FramingLayer.Length = 0x58; // Const for now
	FramingLayer.Priority = 100; // Const for now
	FramingLayer.SynchronizationAddress = 0; // Const for now
	FramingLayer.SequenceNumber = 0; // Const for now
	FramingLayer.Options = 0; // Const for now
	FramingLayer.Universe = UniverseID;

	FDMXProtocolE131DMPLayerPacket DMPLayer;
	if (DMXBuffer->GetDMXData().Num() == ACN_DMX_SIZE)
	{
		FMemory::Memcpy(DMPLayer.DMX, DMXBuffer->GetDMXData().GetData(), ACN_DMX_SIZE);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Size of outcoming sACN DMX buffer is wrong"));
	}
	DMPLayer.Flags = 0x72; // Const for now
	DMPLayer.Length = 0x0b; // Const for now
	DMPLayer.AddressTypeAndDataType = 0xa1; // Const for now
	DMPLayer.FirstPropertyAddress = 0; // Const for now
	DMPLayer.AddressIncrement = ACN_ADDRESS_INC;
	DMPLayer.PropertyValueCount = ACN_DMX_SIZE + 1;
	DMPLayer.STARTCode = 0; // Const for now

	Packager.AddToPackage(&RootLayer);
	Packager.AddToPackage(&FramingLayer);
	Packager.AddToPackage(&DMPLayer);

	// Sending
	FJsonObject PacketSettings;
	PacketSettings.SetNumberField("UniverseID", UniverseID);
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(PacketSettings, Packager.GetBuffer());
	GetSenderInterface()->EnqueueOutboundPackage(Packet);

	return false;
}

void FDMXProtocolSACN::SendRDMCommand(const TSharedPtr<FJsonObject>& CMD)
{
	// Cross protocol RDM send command implementation will be here
}

void FDMXProtocolSACN::RDMDiscovery(const TSharedPtr<FJsonObject>& CMD)
{
	// Cross protocol RDM discovery implementation will be here
}

TSharedPtr<FInternetAddr> FDMXProtocolSACN::GetUniverseAddr(uint16 InUniverseID)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	TSharedPtr<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();
	InternetAddr->SetPort(ACN_PORT);

	FBufferArchive IP;
	uint8 IP_0 = ACN_UNIVERSE_IP_0;
	uint8 IP_1 = ACN_UNIVERSE_IP_1;
	IP << IP_0; // [x.?.?.?]
	IP << IP_1; // [x.x.?.?]
	IP.ByteSwap(&InUniverseID, 2);
	IP << InUniverseID;	// [x.x.x.x]

	InternetAddr->SetRawIp(IP);

	return InternetAddr;
}