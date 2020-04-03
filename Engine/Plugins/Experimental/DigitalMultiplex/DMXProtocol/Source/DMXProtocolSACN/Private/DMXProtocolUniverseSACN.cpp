// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolUniverseSACN.h"

#include "DMXProtocolSACN.h"
#include "DMXProtocolConstants.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolSACNUtils.h"

#include "Common/UdpSocketBuilder.h"
#include "IPAddress.h"
#include "Serialization/ArrayReader.h"
#include "SocketSubsystem.h"

const double FDMXProtocolUniverseSACN::TimeWithoutInputBufferRequest = 5.0;

FDMXProtocolUniverseSACN::FDMXProtocolUniverseSACN(IDMXProtocolPtr InDMXProtocol, const FJsonObject& InSettings)
	: WeakDMXProtocol(InDMXProtocol)
	, ListeningSocket(nullptr)
	, NetworkErrorMessagePrefix(TEXT("NETWORK ERROR SACN:"))
{
	// Not ticking by default
	TimeWithoutInputBufferRequestEnd = TimeWithoutInputBufferRequestStart = FPlatformTime::Seconds();
	bIsTicking = false;

	// Set online subsustem
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	check(SocketSubsystem);

	// Set listener address
	ListenerInternetAddr = SocketSubsystem->CreateInternetAddr();

	// Sets the settings
	Settings = MakeShared<FJsonObject>(InSettings);
	checkf(WeakDMXProtocol.IsValid(), TEXT("DMXProtocol pointer is not valid"));
	checkf(Settings->HasField(TEXT("UniverseID")), TEXT("DMXProtocol UniverseID is not valid"));
	UniverseID = Settings->GetNumberField(TEXT("UniverseID"));

	// Allocate new buffers
	OutputDMXBuffer = MakeShared<FDMXBuffer>();
	InputDMXBuffer = MakeShared<FDMXBuffer>();

	// Set default IP address
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

IDMXProtocolPtr FDMXProtocolUniverseSACN::GetProtocol() const
{
	return WeakDMXProtocol.Pin();
}

TSharedPtr<FDMXBuffer> FDMXProtocolUniverseSACN::GetOutputDMXBuffer() const
{
	return OutputDMXBuffer;
}

TSharedPtr<FDMXBuffer> FDMXProtocolUniverseSACN::GetInputDMXBuffer() const
{
	TimeWithoutInputBufferRequestStart = FPlatformTime::Seconds();
	TimeWithoutInputBufferRequestEnd = TimeWithoutInputBufferRequestStart + TimeWithoutInputBufferRequest;
	bIsTicking = true;

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

bool FDMXProtocolUniverseSACN::ReceiveDMXBuffer()
{
	if (FSocket* Socket = GetOrCreateListeningSocket())
	{
		uint32 Size = 0;
		int32 Read = 0;
		FArrayReaderPtr Reader = MakeShared<FArrayReader, ESPMode::ThreadSafe>(true);

		// Atempt to read if there is any incoming data
		while (Socket->HasPendingData(Size))
		{
			Reader->SetNumUninitialized(FMath::Min(Size, DMX_MAX_PACKET_SIZE));

			// Read buffer from socket
			if (Socket->RecvFrom(Reader->GetData(), Reader->Num(), Read, *ListenerInternetAddr))
			{
				Reader->RemoveAt(Read, Reader->Num() - Read, false);

				return OnDataReceived(Reader);
			}
			else
			{
				ESocketErrors RecvFromError = SocketSubsystem->GetLastErrorCode();

				UE_LOG_DMXPROTOCOL(Error, TEXT("Error recieving the packet Universe ID %d, error is %d"), 
					GetUniverseID(),
					(uint8)RecvFromError);
			}
		}
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("Error recieving. No valid socket for universe %d"), GetUniverseID());
	}


	return false;
}

void FDMXProtocolUniverseSACN::Tick(float DeltaTime)
{
	if (!bIsTicking)
	{
		return;
	}

	if (TimeWithoutInputBufferRequestStart > TimeWithoutInputBufferRequestEnd)
	{
		// Stop listening and destroy the socket
		ReleaseNetworkInterface();

		bIsTicking = false;
	}
	else
	{
		// Keep listening
		ReceiveDMXBuffer();

		TimeWithoutInputBufferRequestStart = FPlatformTime::Seconds();
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
	// Clean the error message
	OutErrorMessage.Empty();

	// Release old network interface
	ReleaseNetworkInterface();

	// Set new network interface IP
	InterfaceIPAddress = InInterfaceIPAddress;

	return true;
}

void FDMXProtocolUniverseSACN::ReleaseNetworkInterface()
{
	// Clean all sockets
	if (ListeningSocket != nullptr)
	{
		ListeningSocket->Close();
		SocketSubsystem->DestroySocket(ListeningSocket);
		ListeningSocket = nullptr;
	}
}

void FDMXProtocolUniverseSACN::SetLayerPackets(const FArrayReaderPtr& Buffer)
{
	*Buffer << IncomingDMXRootLayer;
	*Buffer << IncomingDMXFramingLayer;
	*Buffer << IncomingDMXDMPLayer;
}

bool FDMXProtocolUniverseSACN::OnDataReceived(const FArrayReaderPtr& Buffer)
{
	// It will be more handlers
	switch (SACN::GetRootPacketType(Buffer))
	{
	case VECTOR_ROOT_E131_DATA:
		return HandleReplyPacket(Buffer);
	default:
		return false;
	}
}

bool FDMXProtocolUniverseSACN::HandleReplyPacket(const FArrayReaderPtr& Buffer)
{
	bool bCopySuccessful = false;

	// Copy the data from incoming socket buffer to SACN universe
	SetLayerPackets(Buffer);

	// Access the buffer thread-safety
	InputDMXBuffer->AccessDMXData([this, &bCopySuccessful](TArray<uint8>& InData)
	{
		// Make sure we copy same amount of data
		if (InData.Num() == ACN_DMX_SIZE)
		{
			InputDMXBuffer->SetDMXBuffer(IncomingDMXDMPLayer.DMX, ACN_DMX_SIZE);

			GetProtocol()->GetOnUniverseInputUpdate().Broadcast(GetProtocol()->GetProtocolName(), UniverseID, InData);
			bCopySuccessful = true;
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Error, TEXT("%s: Size of incoming DMX buffer is wrong! Expected: %d; Found: %d")
				, NetworkErrorMessagePrefix
				, ACN_DMX_SIZE
				, InData.Num());
			bCopySuccessful = false;
		}
	});

	return bCopySuccessful;
}

FSocket* FDMXProtocolUniverseSACN::GetOrCreateListeningSocket()
{
	// Simple return the socket if it exists
	if (ListeningSocket != nullptr)
	{
		return ListeningSocket;
	}

	// Try to create IP address at the first
	TSharedPtr<FInternetAddr> InterfaceAddr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	InterfaceAddr->SetIp(*InterfaceIPAddress, bIsValid);
	InterfaceAddr->SetPort(ACN_PORT);
	if (!bIsValid)
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("Wrong IP address: %s"), *InterfaceIPAddress);
		return nullptr;
	}

	FIPv4Endpoint InterfaceEndpoint = FIPv4Endpoint(InterfaceAddr);
	TSharedPtr<FInternetAddr> ListeningAddr = FDMXProtocolSACN::GetUniverseAddr(UniverseID);
	FIPv4Endpoint ListeningEndpoint = FIPv4Endpoint(ListeningAddr);

	FSocket* NewListeningSocket = FUdpSocketBuilder(FString::Printf(TEXT("SACNListeningSocket_Universe_%d"), UniverseID))
		.AsNonBlocking()

#if PLATFORM_WINDOWS
		// For 0.0.0.0, Windows will pick the default interface instead of all
		// interfaces. Here we allow to specify which interface to bind to. 
		// On all other platforms we bind to the wildcard IP address in order
		// to be able to also receive packets that were sent directly to the
		// interface IP instead of the multicast address.
		.BoundToAddress(InterfaceEndpoint.Address)
#endif
		.BoundToPort(InterfaceEndpoint.Port)
#if PLATFORM_SUPPORTS_UDP_MULTICAST_GROUP
		.JoinedToGroup(ListeningEndpoint.Address, InterfaceEndpoint.Address)
		.WithMulticastLoopback()
		.WithMulticastTtl(1)
		.WithMulticastInterface(InterfaceEndpoint.Address)
#endif
		.AsReusable()
		.WithReceiveBufferSize(ACN_DMX_SIZE * 4);

	if (NewListeningSocket == nullptr)
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("Error create ListeningSocket: %s"), *InterfaceIPAddress);
		return nullptr;
	}

	// Save New socket;
	ListeningSocket = NewListeningSocket;

	return ListeningSocket;
}
