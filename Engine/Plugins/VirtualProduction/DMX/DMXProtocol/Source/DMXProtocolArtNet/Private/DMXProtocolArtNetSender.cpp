// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolArtNetSender.h"

#include "DMXProtocolArtNet.h"
#include "DMXProtocolConstants.h"
#include "DMXProtocolLog.h"
#include "DMXProtocolSettings.h"
#include "DMXStats.h"
#include "Packets/DMXProtocolArtNetPackets.h"

#include "IMessageAttachment.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketSender.h"
#include "HAL/RunnableThread.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Art-Net Packages Sent Total"), STAT_ArtNetPackagesSent, STATGROUP_DMX);


FDMXProtocolArtNetSender::FDMXProtocolArtNetSender(const TSharedPtr<FDMXProtocolArtNet, ESPMode::ThreadSafe>& InArtNetProtocol, FSocket& InSocket, TSharedRef<FInternetAddr> InEndpointInternetAddr, EDMXCommunicationType InCommunicationType)
	: Protocol(InArtNetProtocol)
	, Socket(&InSocket)
	, EndpointInternetAddr(InEndpointInternetAddr)
	, CommunicationType(InCommunicationType)
	, bStopping(false)
	, Thread(nullptr)
{
	FString SenderThreadName = FString(TEXT("ArtNetSender_")) + InEndpointInternetAddr->ToString(true);
	Thread = FRunnableThread::Create(this, *SenderThreadName, 0U, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());
}

FDMXProtocolArtNetSender::~FDMXProtocolArtNetSender()
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
}

TSharedPtr<FDMXProtocolArtNetSender> FDMXProtocolArtNetSender::TryCreate(const TSharedPtr<FDMXProtocolArtNet, ESPMode::ThreadSafe>& ArtNetProtocol, const FString& IPAddress, EDMXCommunicationType InCommunicationType)
{
	TSharedPtr<FInternetAddr> EndpointInternetAddr = CreateEndpointInternetAddr(IPAddress);
	if (!EndpointInternetAddr.IsValid())
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Cannot create Art-Net sender: Invalid IP address: %s"), *IPAddress);
		return nullptr;
	}

	FIPv4Endpoint Endpoint = FIPv4Endpoint(EndpointInternetAddr);

	FSocket* NewSenderSocket = nullptr;
	if (InCommunicationType == EDMXCommunicationType::Broadcast)
	{
		NewSenderSocket = FUdpSocketBuilder(TEXT("UDPArtNetBroadcastSocket"))
			.AsNonBlocking()
			.AsReusable()
			.WithBroadcast();
	}
	else if (InCommunicationType == EDMXCommunicationType::Unicast)
	{
		NewSenderSocket = FUdpSocketBuilder(TEXT("UDPArtNetUnicastSocket"))
			.AsNonBlocking()
			.AsReusable()
			.BoundToEndpoint(Endpoint);
	}
	else
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Invalid communciation type specified for Port using IP %s. Please rebuild your Output Ports in Project Settings -> Plugins -> DMX Plugin"), *IPAddress);
		return nullptr;
	}
	
	if(!NewSenderSocket)
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Invalid IP %s for DMX Port. See errors above for further details. Please update your Output Ports in Project Settings -> Plugins -> DMX Plugin"), *IPAddress);
		return nullptr;
	}

	TSharedPtr<FDMXProtocolArtNetSender> NewSender = MakeShareable(new FDMXProtocolArtNetSender(ArtNetProtocol, *NewSenderSocket, EndpointInternetAddr.ToSharedRef(), InCommunicationType));

	return NewSender;
}

bool FDMXProtocolArtNetSender::EqualsEndpoint(const FString& IPAddress, EDMXCommunicationType InCommunicationType) const
{
	if (CommunicationType == InCommunicationType)
	{
		TSharedPtr<FInternetAddr> OtherEndpointInternetAddr = CreateEndpointInternetAddr(IPAddress);
		if (OtherEndpointInternetAddr.IsValid() && OtherEndpointInternetAddr->CompareEndpoints(*EndpointInternetAddr))
		{
			return true;
		}
	}

	return false;
}

void FDMXProtocolArtNetSender::AssignOutputPort(const TSharedPtr<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort)
{
	check(!AssignedOutputPorts.Contains(OutputPort));
	AssignedOutputPorts.Add(OutputPort);
}

void FDMXProtocolArtNetSender::UnassignOutputPort(const TSharedPtr<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort)
{
	check(AssignedOutputPorts.Contains(OutputPort));
	AssignedOutputPorts.Remove(OutputPort);
}

bool FDMXProtocolArtNetSender::IsCausingLoopback() const
{
	return CommunicationType == EDMXCommunicationType::Broadcast;
}

void FDMXProtocolArtNetSender::SendDMXSignal(const FDMXSignalSharedRef& DMXSignal)
{
	Buffer.Enqueue(DMXSignal);
}

void FDMXProtocolArtNetSender::ClearBuffer()
{
	FScopeLock Lock(&LatestSignalLock);

	Buffer.Empty();
	UniverseToLatestSignalMap.Reset();
}

TSharedPtr<FInternetAddr> FDMXProtocolArtNetSender::CreateEndpointInternetAddr(const FString& IPAddress)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	TSharedPtr<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();

	bool bIsValidIP = false;
	InternetAddr->SetIp(*IPAddress, bIsValidIP);
	if (!bIsValidIP)
	{
		return nullptr;
	}

	InternetAddr->SetPort(ARTNET_PORT);
	return InternetAddr;
}

bool FDMXProtocolArtNetSender::Init()
{
	return true;
}

uint32 FDMXProtocolArtNetSender::Run()
{
	const UDMXProtocolSettings* DMXSettings = GetDefault<UDMXProtocolSettings>();
	check(DMXSettings);
	
	float SendDeltaTime = 1.f / DMXSettings->SendingRefreshRate;

	while (!bStopping)
	{
		double StartTime = FPlatformTime::Seconds();

		Update();

		double EndTime = FPlatformTime::Seconds();
		double WaitTime = SendDeltaTime - EndTime - StartTime;

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

void FDMXProtocolArtNetSender::Stop()
{
	bStopping = true;
}

void FDMXProtocolArtNetSender::Exit()
{
}

void FDMXProtocolArtNetSender::Tick()
{
	Update();
}

FSingleThreadRunnable* FDMXProtocolArtNetSender::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolArtNetSender::Update()
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
		const FDMXSignalSharedRef& DMXSignal = UniverseToSignalKvp.Value;

		FDMXProtocolArtNetDMXPacket ArtNetDMXPacket;
		FMemory::Memcpy(ArtNetDMXPacket.Data, DMXSignal->ChannelData.GetData(), ARTNET_DMX_LENGTH);

		uint16 UniverseID = static_cast<uint16>(DMXSignal->ExternUniverseID);

		//Set Packet Data
		ArtNetDMXPacket.Physical = 0; // As per Standard: For information only. We always specify port 0.
		ArtNetDMXPacket.Universe = UniverseID;
		ArtNetDMXPacket.Sequence = 0x00; // As per Standard: The Sequence field is set to 0x00 to disable this feature.

		TSharedPtr<FBufferArchive> BufferArchive = ArtNetDMXPacket.Pack();

		int32 SendDataSize = BufferArchive->Num();
		int32 BytesSent = -1;

		// Avoid spaming the Log
		static bool bErrorEverLogged = false;
		if (Socket->SendTo(BufferArchive->GetData(), BufferArchive->Num(), BytesSent, *EndpointInternetAddr))
		{
			INC_DWORD_STAT(STAT_ArtNetPackagesSent);
		}
		else
		{
			if(!bErrorEverLogged)
			{
				UE_LOG(LogDMXProtocol, Error, TEXT("Failed send DMX to %s"), *EndpointInternetAddr->ToString(false));
				bErrorEverLogged = true;
			}
		}
		
		if (BytesSent != SendDataSize)
		{
			if (!bErrorEverLogged)
			{
				UE_LOG(LogDMXProtocol, Warning, TEXT("Failed send DMX to %s"), *EndpointInternetAddr->ToString(false));
				bErrorEverLogged = true;
			}
		}
	}
}
