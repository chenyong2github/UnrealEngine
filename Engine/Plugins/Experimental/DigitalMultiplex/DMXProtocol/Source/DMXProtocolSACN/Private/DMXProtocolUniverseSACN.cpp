// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolUniverseSACN.h"

#include "DMXProtocolSACN.h"
#include "DMXProtocolConstants.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolTransportSACN.h"
#include "DMXProtocolSettings.h"

#include "Common/UdpSocketBuilder.h"

#include "DMXProtocolSACNUtils.h"

FDMXProtocolUniverseSACN::FDMXProtocolUniverseSACN(TSharedPtr<IDMXProtocol> InDMXProtocol, const FJsonObject& InSettings)
	: WeakDMXProtocol(InDMXProtocol)
	, ListeningSocket(nullptr)
{
	NetworkErrorMessagePrefix = TEXT("NETWORK ERROR SACN:");

	Settings = MakeShared<FJsonObject>(InSettings);

	checkf(WeakDMXProtocol.IsValid(), TEXT("DMXProtocol pointer is not valid"));
	checkf(Settings->HasField(TEXT("UniverseID")), TEXT("DMXProtocol UniverseID is not valid"));
	UniverseID = Settings->GetNumberField(TEXT("UniverseID"));

	OutputDMXBuffer = MakeShared<FDMXBuffer>();
	InputDMXBuffer = MakeShared<FDMXBuffer>();

	InterfaceIPAddress = GetDefault<UDMXProtocolSettings>()->InterfaceIPAddress;

	// Set Network Interface listener
	NetworkInterfaceChangedHandle = IDMXProtocol::OnNetworkInterfaceChanged.AddRaw(this, &FDMXProtocolUniverseSACN::OnNetworkInterfaceChanged);

	// Set Network Interface
	FString ErrorMessage;
	if (!RestartNetworkInterface(InterfaceIPAddress, ErrorMessage))
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:%s"), NetworkErrorMessagePrefix, *ErrorMessage);
	}
}

FDMXProtocolUniverseSACN::~FDMXProtocolUniverseSACN()
{
	ReleaseNetworkInterface();
	IDMXProtocol::OnNetworkInterfaceChanged.Remove(NetworkInterfaceChangedHandle);
}

TSharedPtr<IDMXProtocol> FDMXProtocolUniverseSACN::GetProtocol() const
{
	return WeakDMXProtocol.Pin();
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
	return OutputDMXBuffer->SetDMXFragment(DMXFragment);
}

uint8 FDMXProtocolUniverseSACN::GetPriority() const
{
	return Priority;
}

uint32 FDMXProtocolUniverseSACN::GetUniverseID() const
{
	return UniverseID;
}

TSharedPtr<FJsonObject> FDMXProtocolUniverseSACN::GetSettings() const
{
	return Settings;
}

bool FDMXProtocolUniverseSACN::IsSupportRDM() const
{
	return true;
}

void FDMXProtocolUniverseSACN::OnDataReceived(const FArrayReaderPtr & Buffer)
{
	FScopeLock Lock(&OnDataReceivedCS);

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
		InputDMXBuffer->SetDMXBuffer(IncomingDMXDMPLayer.DMX, ACN_DMX_SIZE);

		GetProtocol()->GetOnUniverseInputUpdate().Broadcast(GetProtocol()->GetProtocolName(), GetUniverseID(), InputDMXBuffer->GetDMXData());
		return true;
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:Size of incoming DMX buffer is wrong"), NetworkErrorMessagePrefix);
		return false;
	}
}

void FDMXProtocolUniverseSACN::OnNetworkInterfaceChanged(const FString& InInterfaceIPAddress)
{
	FString ErrorMessage;
	if (!RestartNetworkInterface(InInterfaceIPAddress, ErrorMessage))
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:%s"), NetworkErrorMessagePrefix, *ErrorMessage);
	}
}

bool FDMXProtocolUniverseSACN::RestartNetworkInterface(const FString& InInterfaceIPAddress, FString& OutErrorMessage)
{
	FScopeLock Lock(&ListeningSocketsCS);

	// Clean the error message
	OutErrorMessage.Empty();

	// Try to create IP address at the first
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedPtr<FInternetAddr> SenderAddr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	SenderAddr->SetIp(*InInterfaceIPAddress, bIsValid);
    //SenderAddr->SetAnyAddress();
	SenderAddr->SetPort(ACN_PORT);
	if (!bIsValid)
	{
		OutErrorMessage = FString::Printf(TEXT("Wrong IP address: %s"), *InInterfaceIPAddress);
		return false;
	}

	// Release old network interface
	ReleaseNetworkInterface();

	FIPv4Endpoint SenderEndpoint = FIPv4Endpoint(SenderAddr);
	TSharedPtr<FInternetAddr> ListeningAddr = FDMXProtocolSACN::GetUniverseAddr(UniverseID);
	FIPv4Endpoint ListeningEndpoint = FIPv4Endpoint(ListeningAddr);

	FSocket* NewListeningSocket = FUdpSocketBuilder(TEXT("SACNListeningSocket"))
		.AsNonBlocking()

#if PLATFORM_WINDOWS
		// For 0.0.0.0, Windows will pick the default interface instead of all
		// interfaces. Here we allow to specify which interface to bind to. 
		// On all other platforms we bind to the wildcard IP address in order
		// to be able to also receive packets that were sent directly to the
		// interface IP instead of the multicast address.
		.BoundToAddress(SenderEndpoint.Address)
#endif
		.BoundToPort(SenderEndpoint.Port)
#if PLATFORM_SUPPORTS_UDP_MULTICAST_GROUP
		.JoinedToGroup(ListeningEndpoint.Address, SenderEndpoint.Address)
		.WithMulticastLoopback()
		.WithMulticastTtl(1)
		.WithMulticastInterface(SenderEndpoint.Address)
#endif
        .AsReusable();


	if (NewListeningSocket == nullptr)
	{
		OutErrorMessage = FString::Printf(TEXT("Error create ListeningSocket: %s"), *InterfaceIPAddress);
		return false;
	}

	// Set new network interface IP
	InterfaceIPAddress = InInterfaceIPAddress;

	// Save New socket;
	ListeningSocket = NewListeningSocket;

	// Set new receiver
	SACNReceiver = MakeShared<FDMXProtocolReceiverSACN>(*ListeningSocket, FTimespan::FromMilliseconds(100));
	SACNReceiver->OnDataReceived().BindRaw(this, &FDMXProtocolUniverseSACN::OnDataReceived);

	return true;
}

void FDMXProtocolUniverseSACN::ReleaseNetworkInterface()
{
	if (SACNReceiver.IsValid())
	{
		SACNReceiver.Reset();
		SACNReceiver = nullptr;
	}

	// Clean all sockets
	if (ListeningSocket != nullptr)
	{
		ListeningSocket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(ListeningSocket);
		}
		ListeningSocket = nullptr;
	}
}
