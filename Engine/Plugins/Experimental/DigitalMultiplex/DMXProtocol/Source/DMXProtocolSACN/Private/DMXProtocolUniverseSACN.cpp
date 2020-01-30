// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolUniverseSACN.h"

#include "DMXProtocolSACN.h"
#include "Interfaces/IDMXProtocolDevice.h"
#include "DMXProtocolConstants.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolTransportSACN.h"
#include "Common/UdpSocketBuilder.h"

#include "DMXProtocolSACNUtils.h"

FDMXProtocolUniverseSACN::FDMXProtocolUniverseSACN(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolPort> InPort, uint16 InUniverseID)
	: DMXProtocol(InDMXProtocol)
	, Port(InPort)
	, Priority(0)
	, UniverseID(InUniverseID)
{
	checkf(DMXProtocol, TEXT("DMXProtocol pointer is nullptr"));
	checkf(Port.IsValid(), TEXT("DMXProtocol port is not valid"));

	OutputDMXBuffer = MakeShared<FDMXBuffer>();
	InputDMXBuffer = MakeShared<FDMXBuffer>();

	FIPv4Endpoint UnicastEndpoint = FIPv4Endpoint::Any;
	TSharedPtr<FInternetAddr> MulticastAddr = FDMXProtocolSACN::GetUniverseAddr(UniverseID);
	FIPv4Endpoint MulticastEndpoint = FIPv4Endpoint(MulticastAddr);

	ListenerSocket = FUdpSocketBuilder(TEXT("UDPSACNListenerSocket"))
		.AsNonBlocking()
		.AsReusable()
#if PLATFORM_WINDOWS
		.BoundToAddress(UnicastEndpoint.Address)
#endif
		.BoundToPort(MulticastEndpoint.Port)
		.JoinedToGroup(MulticastEndpoint.Address, UnicastEndpoint.Address)
		.WithMulticastLoopback()
		.WithMulticastTtl(1)
		.WithMulticastInterface(UnicastEndpoint.Address)
		.WithReceiveBufferSize(2 * 1024 * 1024);

	if (ListenerSocket == nullptr)
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("ERROR create BroadcastSocket"));
	}
	else
	{
		SACNReceiver = MakeShared<FDMXProtocolReceiverSACN>(*ListenerSocket, FTimespan::FromMilliseconds(100));
		SACNReceiver->OnDataReceived().BindRaw(this, &FDMXProtocolUniverseSACN::OnDataReceived);
	}
}

FDMXProtocolUniverseSACN::~FDMXProtocolUniverseSACN()
{
	SACNReceiver.Reset();

	// Clear all sockets
	if (ListenerSocket)
	{
		ListenerSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket);
	}
}

IDMXProtocol * FDMXProtocolUniverseSACN::GetProtocol() const
{
	return DMXProtocol;
}

TWeakPtr<IDMXProtocolPort> FDMXProtocolUniverseSACN::GetCachedUniversePort() const
{
	return Port;
}

TSharedPtr<FDMXBuffer> FDMXProtocolUniverseSACN::GetOutputDMXBuffer() const
{
	return OutputDMXBuffer;
}

TSharedPtr<FDMXBuffer> FDMXProtocolUniverseSACN::GetInputDMXBuffer() const
{
	return InputDMXBuffer;
}

bool FDMXProtocolUniverseSACN::SetDMXFragment(const IDMXFragmentMap & DMXFragment)
{
	OutputDMXBuffer->SetDMXFragment(DMXFragment);
	return false;
}

uint8 FDMXProtocolUniverseSACN::GetPriority() const
{
	return Priority;
}

uint16 FDMXProtocolUniverseSACN::GetUniverseID() const
{
	return UniverseID;
}

void FDMXProtocolUniverseSACN::OnDataReceived(const FArrayReaderPtr & Buffer)
{
	// It will be more handlers
	switch (SACN::GetRootPacketType(Buffer))
	{
	case VECTOR_ROOT_E131_DATA:
		HandleReplyPacket(Buffer);
		break;
	default:
		break;
	}
}

bool FDMXProtocolUniverseSACN::HandleReplyPacket(const FArrayReaderPtr & Buffer)
{
	// SACN PDU packets
	*Buffer << IncomingDMXRootLayer;
	*Buffer << IncomingDMXFramingLayer;
	*Buffer << IncomingDMXDMPLayer;

	// Make sure we copy same amount of data
	if (InputDMXBuffer->GetDMXData().Num() == ACN_DMX_SIZE)
	{
		FMemory::Memcpy(InputDMXBuffer->GetDMXData().GetData(), IncomingDMXDMPLayer.DMX, ACN_DMX_SIZE);
		return true;
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Size of incoming DMX buffer is wrong"));
		return false;
	}
}
