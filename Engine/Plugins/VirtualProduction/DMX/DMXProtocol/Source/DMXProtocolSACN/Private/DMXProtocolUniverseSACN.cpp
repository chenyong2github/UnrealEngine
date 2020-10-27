// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolUniverseSACN.h"

#include "DMXProtocolSACN.h"
#include "DMXProtocolConstants.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolTransportSACN.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolSACNReceivingRunnable.h"

#include "Common/UdpSocketBuilder.h"
#include "Serialization/ArrayReader.h"
#include "SocketSubsystem.h"

#include "DMXProtocolSACNUtils.h"
#include "DMXStats.h"

DECLARE_MEMORY_STAT(TEXT("SACN Input And Output Buffer Memory"), STAT_SACNInputAndOutputBufferMemory, STATGROUP_DMX);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("SACN Universes Count"), STAT_SACNUniversesCount, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("SACN Packages Recieved"), STAT_SACNPackagesRecieved, STATGROUP_DMX);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("SACN Packages Recieved Total"), STAT_SACNPackagesRecievedTotal, STATGROUP_DMX);

FDMXProtocolUniverseSACN::FDMXProtocolUniverseSACN(TWeakPtr<FDMXProtocolSACN, ESPMode::ThreadSafe> DMXProtocolSACN, const FJsonObject& InSettings)
	: HighestReceivedPriority(0)
	, WeakDMXProtocol(DMXProtocolSACN)
	, bShouldReceiveDMX(true)
	, ListeningSocket(nullptr)
	, NetworkErrorMessagePrefix(TEXT("NETWORK ERROR SACN:"))
{
	// Set online subsustem
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	check(SocketSubsystem);
	// Set listener address
	ListenerInternetAddr = SocketSubsystem->CreateInternetAddr();

	UpdateSettings(InSettings);

	checkf(WeakDMXProtocol.IsValid(), TEXT("DMXProtocol pointer is not valid"));

	// Allocate new buffers
	OutputDMXBuffer = MakeShared<FDMXBuffer, ESPMode::ThreadSafe>();
	InputDMXBuffer = MakeShared<FDMXBuffer, ESPMode::ThreadSafe>();

	// Stats
	INC_MEMORY_STAT_BY(STAT_SACNInputAndOutputBufferMemory, DMX_UNIVERSE_SIZE * 2);
	INC_DWORD_STAT(STAT_SACNUniversesCount);

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
	if (IDMXProtocolPtr Protocol = WeakDMXProtocol.Pin())
	{
		return StaticCastSharedPtr<IDMXProtocol>(Protocol);
	}
	return nullptr;
}

FDMXBufferPtr FDMXProtocolUniverseSACN::GetInputDMXBuffer() const
{
	return InputDMXBuffer;
}

FDMXBufferPtr FDMXProtocolUniverseSACN::GetOutputDMXBuffer() const
{
	return OutputDMXBuffer;
}

void FDMXProtocolUniverseSACN::ZeroInputDMXBuffer()
{
	InputDMXBuffer->ZeroDMXBuffer();
}

void FDMXProtocolUniverseSACN::ZeroOutputDMXBuffer()
{
	OutputDMXBuffer->ZeroDMXBuffer();
}

bool FDMXProtocolUniverseSACN::SetDMXFragment(const IDMXFragmentMap& DMXFragment)
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

void FDMXProtocolUniverseSACN::UpdateSettings(const FJsonObject& InSettings)
{
	Settings = MakeShared<FJsonObject>(InSettings);
	checkf(Settings->HasField(DMXJsonFieldNames::DMXUniverseID), TEXT("DMXProtocol UniverseID is not valid"));
	checkf(Settings->HasField(DMXJsonFieldNames::DMXEthernetPort), TEXT("DMXProtocol EthernPort is not valid"));
	checkf(Settings->HasField(DMXJsonFieldNames::DMXIpAddresses), TEXT("DMXProtocol IPAddresses is not valid"));
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

bool FDMXProtocolUniverseSACN::IsSupportRDM() const
{
	return true;
}

void FDMXProtocolUniverseSACN::Tick(float DeltaTime)
{
	if (bShouldReceiveDMX)
	{
		const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();

		ReceiveIncomingData();
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

void FDMXProtocolUniverseSACN::CreateDMXListener()
{
	// The listener will be created on demand, see FDMXProtocolUniverseSACN::ReceiveIncomingData
	bShouldReceiveDMX = true;
}

void FDMXProtocolUniverseSACN::DestroyDMXListener()
{
	bShouldReceiveDMX = false;

	// Release any existing listener
	ReleaseNetworkInterface();
}

void FDMXProtocolUniverseSACN::SetLayerPackets(const FArrayReaderPtr& Buffer)
{
	*Buffer << IncomingDMXRootLayer;
	*Buffer << IncomingDMXFramingLayer;
	*Buffer << IncomingDMXDMPLayer;
}

void FDMXProtocolUniverseSACN::ReceiveIncomingData()
{
	if (FSocket* Socket = GetOrCreateListeningSocket())
	{
		uint32 Size = 0;
		int32 Read = 0;
		FArrayReaderPtr Reader = MakeShared<FArrayReader, ESPMode::ThreadSafe>(true);

		// Atempt to read if there is any incoming data
		// We don't use Socket Wait() function because we might have a lot of listening sockets in the loop
		// And it might cost the delayы in the thread
		while (Socket->HasPendingData(Size))
		{
			Reader->SetNumUninitialized(FMath::Min(Size, DMX_MAX_PACKET_SIZE));

			// Read buffer from socket
			if (Socket->RecvFrom(Reader->GetData(), Reader->Num(), Read, *ListenerInternetAddr))
			{
				// Stats
				SCOPE_CYCLE_COUNTER(STAT_SACNPackagesRecieved);
				INC_DWORD_STAT(STAT_SACNPackagesRecievedTotal);

				Reader->RemoveAt(Read, Reader->Num() - Read, false);

				OnDataReceived(Reader);
			}
			else
			{
				ESocketErrors RecvFromError = SocketSubsystem->GetLastErrorCode();
				UE_LOG_DMXPROTOCOL(Error, TEXT("Error Receiving the packet Universe ID %d, error is %d"),
					GetUniverseID(),
					(uint8)RecvFromError);
			}
		}
	}
}

void FDMXProtocolUniverseSACN::OnDataReceived(const FArrayReaderPtr& Buffer)
{
	// It will be more handlers
	switch (SACN::GetRootPacketType(Buffer))
	{
	case VECTOR_ROOT_E131_DATA:
		HandleReplyPacket(Buffer);
		return;
	default:
		return;
	}
}

void FDMXProtocolUniverseSACN::HandleReplyPacket(const FArrayReaderPtr& Buffer)
{
	// Copy the data from incoming socket buffer to SACN universe
	SetLayerPackets(Buffer);

	// Ignore packets of lower priority than the highest one we received.
	if (HighestReceivedPriority > IncomingDMXFramingLayer.Priority)
	{
		return;
	}
	HighestReceivedPriority = IncomingDMXFramingLayer.Priority;

	// Make sure we copy same amount of data
	if (WeakDMXProtocol.IsValid())
	{
		if (TSharedPtr<FDMXProtocolSACNReceivingRunnable, ESPMode::ThreadSafe> ReceivingRunnable = WeakDMXProtocol.Pin()->GetReceivingRunnable())
		{
			ReceivingRunnable->PushDMXPacket(GetUniverseID(), IncomingDMXDMPLayer);
		}
	}
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

	TSharedPtr<FInternetAddr> ListeningAddr = SocketSubsystem->CreateInternetAddr();
	ListeningAddr->SetIp(FDMXProtocolSACN::GetUniverseAddrByID(UniverseID));
	ListeningAddr->SetPort(ACN_PORT);
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
