// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACNSender.h"

#include "DMXProtocolSACN.h"
#include "DMXProtocolSACNReceiver.h"

#include "DMXProtocolConstants.h"
#include "DMXProtocolLog.h"
#include "DMXProtocolSettings.h"
#include "DMXStats.h"
#include "Packets/DMXProtocolE131PDUPacket.h"

#include "IMessageAttachment.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketSender.h"
#include "HAL/RunnableThread.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"
#include "UObject/Class.h"

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("sACN Packages Sent Total"), STAT_SACNPackagesSent, STATGROUP_DMX);

namespace
{
	/** Helper to create an internet address from an IP address string. Returns the InternetAddr or nullptr if unsuccessful */
	TSharedPtr<FInternetAddr> CreateInternetAddr(const FString& IPAddress, int32 Port)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

		TSharedPtr<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();

		bool bIsValidIP = false;
		InternetAddr->SetIp(*IPAddress, bIsValidIP);
		if (!bIsValidIP)
		{
			return nullptr;
		}

		InternetAddr->SetPort(Port);
		return InternetAddr;
	}
}


FDMXProtocolSACNSender::FDMXProtocolSACNSender(const TSharedPtr<FDMXProtocolSACN, ESPMode::ThreadSafe>& InSACNProtocol, FSocket& InSocket, TSharedRef<FInternetAddr> InNetworkInterfaceInternetAddr, TSharedRef<FInternetAddr> InDestinationInternetAddr, const bool bInIsMulticast)
	: Protocol(InSACNProtocol)
	, Socket(&InSocket)
	, NetworkInterfaceInternetAddr(InNetworkInterfaceInternetAddr)
	, DestinationInternetAddr(InDestinationInternetAddr)
	, bStopping(false)
	, Thread(nullptr)
	, bIsMulticast(bInIsMulticast)
{
	check(DestinationInternetAddr.IsValid());

	FString SenderThreadName = FString(TEXT("sACNSender_")) + InDestinationInternetAddr->ToString(false);
	Thread = FRunnableThread::Create(this, *SenderThreadName, 0U, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

	if (bIsMulticast)
	{
		UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Created sACN Multicast Sender at %s"), *NetworkInterfaceInternetAddr->ToString(false));
	}
	else
	{
		UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Created sACN Sender at %s sending to %s"), *NetworkInterfaceInternetAddr->ToString(false), *DestinationInternetAddr->ToString(false));
	}
}

FDMXProtocolSACNSender::~FDMXProtocolSACNSender()
{
	if (Socket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		SocketSubsystem->DestroySocket(Socket);
	}

	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Destroyed sACN Sender at %s sending to %s"), *NetworkInterfaceInternetAddr->ToString(false), *DestinationInternetAddr->ToString(false));
}

TSharedPtr<FDMXProtocolSACNSender> FDMXProtocolSACNSender::TryCreateUnicastSender(
	const TSharedPtr<FDMXProtocolSACN,
	ESPMode::ThreadSafe>& SACNProtocol,
	const FString& InNetworkInterfaceIP,
	const FString& InUnicastIP)
{
	// Try to create a socket
	TSharedPtr<FInternetAddr> NewNetworkInterfaceInternetAddr = CreateInternetAddr(InNetworkInterfaceIP, ACN_PORT);
	if (!NewNetworkInterfaceInternetAddr.IsValid())
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Cannot create sACN sender: Invalid IP address: %s"), *InNetworkInterfaceIP);
		return nullptr;
	}

	FIPv4Endpoint NewNetworkInterfaceEndpoint = FIPv4Endpoint(NewNetworkInterfaceInternetAddr);
	
	FSocket* NewSocket =
		FUdpSocketBuilder(TEXT("UDPSACNUnicastSocket"))
		.AsBlocking()
		.AsReusable()
		.BoundToEndpoint(NewNetworkInterfaceEndpoint)
		.WithMulticastLoopback();

	if(!NewSocket)
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Invalid Network Interface IP %s for DMX Port. Please update your Output Port in Project Settings -> Plugins -> DMX Plugin"), *InNetworkInterfaceIP);
		return nullptr;
	}

	// Try create the unicast internet addr
	TSharedPtr<FInternetAddr> NewUnicastInternetAddr = CreateInternetAddr(InUnicastIP, ACN_PORT);
	if (!NewUnicastInternetAddr.IsValid())
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Invalid Unicast IP %s for DMX Port. Please update your Output Port in Project Settings -> Plugins -> DMX Plugin"), *InUnicastIP);
		return nullptr;
	}

	TSharedPtr<FDMXProtocolSACNSender> NewSender = MakeShareable(new FDMXProtocolSACNSender(SACNProtocol, *NewSocket, NewNetworkInterfaceInternetAddr.ToSharedRef(), NewUnicastInternetAddr.ToSharedRef(), false));

	return NewSender;
}

TSharedPtr<FDMXProtocolSACNSender> FDMXProtocolSACNSender::TryCreateMulticastSender(
	const TSharedPtr<FDMXProtocolSACN,
	ESPMode::ThreadSafe>& SACNProtocol,
	const FString& InNetworkInterfaceIP)
{
	// Try to create a socket
	TSharedPtr<FInternetAddr> NewNetworkInterfaceInternetAddr = CreateInternetAddr(InNetworkInterfaceIP, ACN_PORT);
	if (!NewNetworkInterfaceInternetAddr.IsValid())
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Cannot create sACN sender: Invalid IP address: %s"), *InNetworkInterfaceIP);
		return nullptr;
	}

	FIPv4Endpoint NewNetworkInterfaceEndpoint = FIPv4Endpoint(NewNetworkInterfaceInternetAddr);

	FSocket* NewSocket = 
		FUdpSocketBuilder(TEXT("UDPSACNMulticastSocket"))
#if PLATFORM_SUPPORTS_UDP_MULTICAST_GROUP
		.WithMulticastInterface(NewNetworkInterfaceEndpoint.Address)
		.WithMulticastTtl(1)
#endif
		.AsBlocking()
		.BoundToEndpoint(NewNetworkInterfaceInternetAddr)
		.AsReusable();

	if(!NewSocket)
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Invalid Network Interface IP %s for DMX Port. Please update your Output Ports in Project Settings -> Plugins -> DMX Plugin"), *InNetworkInterfaceIP);
		return nullptr;
	}

	TSharedPtr<FDMXProtocolSACNSender> NewSender = MakeShareable(new FDMXProtocolSACNSender(SACNProtocol, *NewSocket, NewNetworkInterfaceInternetAddr.ToSharedRef(), NewNetworkInterfaceInternetAddr.ToSharedRef(), true));

	return NewSender;
}

bool FDMXProtocolSACNSender::EqualsEndpoint(const FString& NetworkInterfaceIP, const FString& DestinationIPAddress) const
{
	TSharedPtr<FInternetAddr> OtherNetworkInterfaceInternetAddr = CreateInternetAddr(NetworkInterfaceIP, ACN_PORT);
	if (OtherNetworkInterfaceInternetAddr.IsValid() && OtherNetworkInterfaceInternetAddr->CompareEndpoints(*NetworkInterfaceInternetAddr))
	{
		TSharedPtr<FInternetAddr> OtherDestinationInternetAddr = CreateInternetAddr(DestinationIPAddress, ACN_PORT);
		if (OtherDestinationInternetAddr.IsValid() && OtherDestinationInternetAddr->CompareEndpoints(*DestinationInternetAddr))
		{
			return true;
		}
	}

	return false;
}

void FDMXProtocolSACNSender::AssignOutputPort(const TSharedPtr<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort)
{
	check(!AssignedOutputPorts.Contains(OutputPort));
	AssignedOutputPorts.Add(OutputPort);
}

void FDMXProtocolSACNSender::UnassignOutputPort(const TSharedPtr<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort)
{
	check(AssignedOutputPorts.Contains(OutputPort));
	AssignedOutputPorts.Remove(OutputPort);
}

bool FDMXProtocolSACNSender::IsCausingLoopback() const
{
	return CommunicationType == EDMXCommunicationType::Broadcast;
}

void FDMXProtocolSACNSender::SendDMXSignal(const FDMXSignalSharedRef& DMXSignal)
{
	Buffer.Enqueue(DMXSignal);
}

void FDMXProtocolSACNSender::ClearBuffer()
{
	FScopeLock Lock(&LatestSignalLock);

	Buffer.Empty();
	UniverseToLatestSignalMap.Reset();
}

bool FDMXProtocolSACNSender::Init()
{
	return true;
}

uint32 FDMXProtocolSACNSender::Run()
{
	const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>();
	check(DMXSettings);
	
	// Fixed rate delta time
	const double SendDeltaTime = 1.f / DMXSettings->SendingRefreshRate;

	while (!bStopping)
	{
		const double StartTime = FPlatformTime::Seconds();

		Update();

		const double EndTime = FPlatformTime::Seconds();
		const double WaitTime = SendDeltaTime - (EndTime - StartTime);

		if (WaitTime > 0.f)
		{
			// Sleep by the amount which is set in refresh rate
			FPlatformProcess::SleepNoStats(WaitTime);
		}

		// In the unlikely case we took to long to send, we instantly continue, but do not take 
		// further measures to compensate - We would have to run faster than DMX send rate to catch up.
	}

	return 0;
}

void FDMXProtocolSACNSender::Stop()
{
	bStopping = true;
}

void FDMXProtocolSACNSender::Exit()
{
}

void FDMXProtocolSACNSender::Tick()
{
	Update();
}

FSingleThreadRunnable* FDMXProtocolSACNSender::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolSACNSender::Update()
{
	FScopeLock Lock(&LatestSignalLock);

	// Keep latest signal per universe
	FDMXSignalSharedPtr DequeuedDMXSignal;
	while (Buffer.Dequeue(DequeuedDMXSignal))
	{
		if (Protocol->IsValidUniverseID(DequeuedDMXSignal->ExternUniverseID))
		{
			if(UniverseToLatestSignalMap.Contains(DequeuedDMXSignal->ExternUniverseID))
			{
				UniverseToLatestSignalMap[DequeuedDMXSignal->ExternUniverseID] = DequeuedDMXSignal.ToSharedRef();
			}
			else
			{
				UniverseToLatestSignalMap.Add(DequeuedDMXSignal->ExternUniverseID, DequeuedDMXSignal.ToSharedRef());
			}
		}
	}

	// Create a packet for each universe and send it
	for (const TTuple<int32, FDMXSignalSharedRef>& UniverseToSignalKvp : UniverseToLatestSignalMap)
	{
		int32 UniverseID = UniverseToSignalKvp.Key;
		const FDMXSignalSharedRef& DMXSignal = UniverseToSignalKvp.Value;

		TArray<uint8> Packet;

		FDMXProtocolE131RootLayerPacket RootLayer;
		static FGuid Guid = FGuid::NewGuid();
		FMemory::Memcpy(RootLayer.CID.GetData(), &Guid, ACN_CIDBYTES);

		Packet.Append(*RootLayer.Pack(ACN_DMX_SIZE));

		FDMXProtocolE131FramingLayerPacket FramingLayer;
		FramingLayer.Universe = UniverseID;
		FramingLayer.SequenceNumber = UniverseIDToSequenceNumberMap.FindOrAdd(UniverseID, -1)++; // Init to max, let it wrap over to 0 at first
		FramingLayer.Priority = DMXSignal->Priority;
		Packet.Append(*FramingLayer.Pack(ACN_DMX_SIZE));

		FDMXProtocolE131DMPLayerPacket DMPLayer;
		DMPLayer.AddressIncrement = ACN_ADDRESS_INC;
		DMPLayer.PropertyValueCount = ACN_DMX_SIZE + 1;
		FMemory::Memcpy(DMPLayer.DMX.GetData(), DMXSignal->ChannelData.GetData(), ACN_DMX_SIZE);

		Packet.Append(*DMPLayer.Pack(ACN_DMX_SIZE));

		const int32 SendDataSize = Packet.Num();
		int32 BytesSent = -1;

		// Try to send, log errors but avoid spaming the Log
		static bool bErrorEverLogged = false;
		TSharedPtr<FInternetAddr> CurrentDestination = DestinationInternetAddr;

		// if in multicast, compute the destination
		if (bIsMulticast)
		{
			ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			CurrentDestination = SocketSubsystem->CreateInternetAddr();

			uint32 IpForUniverse = FDMXProtocolSACNReceiver::GetIpForUniverseID(UniverseID);
			CurrentDestination->SetIp(IpForUniverse);
			CurrentDestination->SetPort(ACN_PORT);
		}

		if (Socket->SendTo(Packet.GetData(), Packet.Num(), BytesSent, *CurrentDestination))
		{
			INC_DWORD_STAT(STAT_SACNPackagesSent);
		}
		else
		{
			if(!bErrorEverLogged)
			{
				ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
				TEnumAsByte<ESocketErrors> RecvFromError = SocketSubsystem->GetLastErrorCode();

				UE_LOG(LogDMXProtocol, Error, TEXT("Failed send DMX to %s with Error Code %d"), *DestinationInternetAddr->ToString(false), RecvFromError);

				bErrorEverLogged = true;
			}
		}
		
		if (BytesSent != SendDataSize)
		{
			if (!bErrorEverLogged)
			{
				UE_LOG(LogDMXProtocol, Warning, TEXT("Incomplete DMX Packet sent to %s"), *DestinationInternetAddr->ToString(false));
				bErrorEverLogged = true;
			}
		}
	}
}
