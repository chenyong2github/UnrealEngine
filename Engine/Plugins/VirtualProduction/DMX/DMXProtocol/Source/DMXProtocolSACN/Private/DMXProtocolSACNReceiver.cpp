// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACNReceiver.h"

#include "DMXProtocolLog.h"
#include "DMXProtocolSACN.h"
#include "DMXProtocolSACNConstants.h"
#include "DMXStats.h"
#include "IO/DMXInputPort.h"
#include "Packets/DMXProtocolE131PDUPacket.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Common/UdpSocketBuilder.h"
#include "HAL/RunnableThread.h"
#include "Serialization/ArrayReader.h"

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("sACN Packages Recieved Total"), STAT_SACNPackagesReceived, STATGROUP_DMX);


namespace
{
	static TSharedPtr<FInternetAddr> CreateEndpointInternetAddr(const FString& IPAddress)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

		TSharedPtr<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();

		bool bIsValidIP = false;
		InternetAddr->SetIp(*IPAddress, bIsValidIP);
		if (!bIsValidIP)
		{
			return nullptr;
		}

		InternetAddr->SetPort(ACN_PORT);
		return InternetAddr;
	}
}

FDMXProtocolSACNReceiver::FDMXProtocolSACNReceiver(const TSharedPtr<FDMXProtocolSACN, ESPMode::ThreadSafe>& InSACNProtocol, FSocket& InSocket, TSharedRef<FInternetAddr> InEndpointInternetAddr)
	: HighestReceivedPriority(0)
	, Protocol(InSACNProtocol)
	, Socket(&InSocket)
	, EndpointInternetAddr(InEndpointInternetAddr)
	, bStopping(false)
	, Thread(nullptr)
{
	check(Socket->GetSocketType() == SOCKTYPE_Datagram);

	FString ReceiverThreadName = FString(TEXT("SACNReceiver_")) + InEndpointInternetAddr->ToString(true);
	Thread = FRunnableThread::Create(this, *ReceiverThreadName, 0, TPri_TimeCritical, FPlatformAffinity::GetPoolThreadMask());

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Created sACN Receiver at %s"), *EndpointInternetAddr->ToString(false));
}

FDMXProtocolSACNReceiver::~FDMXProtocolSACNReceiver()
{
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}

	if (Socket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		SocketSubsystem->DestroySocket(Socket);
	}

	UE_LOG(LogDMXProtocol, VeryVerbose, TEXT("Destroyed sACN Receiver at %s"), *EndpointInternetAddr->ToString(false));
}

TSharedPtr<FDMXProtocolSACNReceiver> FDMXProtocolSACNReceiver::TryCreate(const TSharedPtr<FDMXProtocolSACN, ESPMode::ThreadSafe>& SACNProtocol, const FString& IPAddress)
{
	TSharedPtr<FInternetAddr> EndpointInternetAddr = CreateEndpointInternetAddr(IPAddress);
	if (!EndpointInternetAddr.IsValid())
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Cannot create sACN receiver: Invalid IP address: %s"), *IPAddress);
		return nullptr;
	}

	FIPv4Endpoint Endpoint = FIPv4Endpoint(EndpointInternetAddr);

	FSocket* NewListeningSocket = FUdpSocketBuilder(TEXT("UDPSACNListeningSocket"))
		.AsBlocking()
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.WithMulticastLoopback()
		.WithMulticastTtl(1)
		.WithMulticastInterface(Endpoint.Address);

	if (!NewListeningSocket)
	{
		UE_LOG(LogDMXProtocol, Error, TEXT("Cannot create sACN receiver: Error create ListeningSocket for: %s"), *IPAddress);
		return nullptr;
	}

	if (!NewListeningSocket->SetIpPktInfo(true))
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		SocketSubsystem->DestroySocket(NewListeningSocket);

		UE_LOG(LogDMXProtocol, Error, TEXT("Cannot create sACN receiver: Platform does not support IP_PKTINFO required for sACN."), *IPAddress);
		return nullptr;
	}

	TSharedPtr<FDMXProtocolSACNReceiver> NewReceiver = MakeShareable(new FDMXProtocolSACNReceiver(SACNProtocol, *NewListeningSocket, EndpointInternetAddr.ToSharedRef()));

	return NewReceiver;
}

bool FDMXProtocolSACNReceiver::EqualsEndpoint(const FString& IPAddress) const
{
	TSharedPtr<FInternetAddr> OtherEndpointInternetAddr = CreateEndpointInternetAddr(IPAddress);
	if (OtherEndpointInternetAddr.IsValid() && OtherEndpointInternetAddr->CompareEndpoints(*EndpointInternetAddr))
	{
		return true;
	}

	return false;
}

void FDMXProtocolSACNReceiver::AssignInputPort(const TSharedPtr<FDMXInputPort, ESPMode::ThreadSafe>& InputPort)
{
	check(!AssignedInputPorts.Contains(InputPort));
	AssignedInputPorts.Add(InputPort);

	// Join the multicast groups for the ports where needed
	uint16 UniverseIDStart = InputPort->GetExternUniverseStart();
	uint16 UniverseIDEnd = InputPort->GetExternUniverseEnd();

	TArray<uint16> ExistingMulticastGroupUniverseIDs;
	MulticastGroupAddrToUniverseIDMap.GenerateValueArray(ExistingMulticastGroupUniverseIDs);


	for (int32 UniverseID = UniverseIDStart; UniverseID <= UniverseIDEnd; UniverseID++)
	{
		if (!ExistingMulticastGroupUniverseIDs.Contains(UniverseID))
		{
			ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			TSharedRef<FInternetAddr> NewMulticastGroupAddr = SocketSubsystem->CreateInternetAddr();

			uint32 MulticastIp = GetIpForUniverseID(UniverseID);
			NewMulticastGroupAddr->SetIp(MulticastIp);
			NewMulticastGroupAddr->SetPort(ACN_PORT);

			Socket->JoinMulticastGroup(*NewMulticastGroupAddr);

			MulticastGroupAddrToUniverseIDMap.Add(MulticastIp, UniverseID);
		}
	}
}

void FDMXProtocolSACNReceiver::UnassignInputPort(const TSharedPtr<FDMXInputPort, ESPMode::ThreadSafe>& InputPort)
{
	check(AssignedInputPorts.Contains(InputPort));
	AssignedInputPorts.Remove(InputPort);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> MulticastGroupAddrToLeave = SocketSubsystem->CreateInternetAddr();

	// Leave multicast groups outside of remaining port's universe ranges
	for (const TTuple<uint32, uint16>& MulticastGroupAddrToUniverseIDKvp : MulticastGroupAddrToUniverseIDMap)
	{
		MulticastGroupAddrToLeave->SetIp(MulticastGroupAddrToUniverseIDKvp.Key);
		MulticastGroupAddrToLeave->SetPort(ACN_PORT);
		uint16 UniverseID = MulticastGroupAddrToUniverseIDKvp.Value;

		bool bGroupInUse = true;
		for (const FDMXInputPortSharedPtr& Port : AssignedInputPorts)
		{
			if (Port->GetExternUniverseStart() >= UniverseID &&
				Port->GetExternUniverseEnd() <= UniverseID)
			{
				continue;
			}

			bGroupInUse = false;
			break;
		}

		if (!bGroupInUse)
		{
			Socket->LeaveMulticastGroup(*MulticastGroupAddrToLeave);
		}
	}
}

bool FDMXProtocolSACNReceiver::Init()
{
	return true;
}

uint32 FDMXProtocolSACNReceiver::Run()
{
	// No receive refresh rate, it would deter the timestamp
	
	while (!bStopping)
	{
		Update(FTimespan::FromMilliseconds(1000.f));
	}

	return 0;
}

void FDMXProtocolSACNReceiver::Stop()
{
	bStopping = true;
}

void FDMXProtocolSACNReceiver::Exit()
{
}

void FDMXProtocolSACNReceiver::Tick()
{
	Update(FTimespan::Zero());
}

FSingleThreadRunnable* FDMXProtocolSACNReceiver::GetSingleThreadInterface()
{
	return this;
}

void FDMXProtocolSACNReceiver::Update(const FTimespan& SocketWaitTime)
{
	if (!Socket->Wait(ESocketWaitConditions::WaitForRead, SocketWaitTime))
	{
		return;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedPtr<FInternetAddr> Sender = SocketSubsystem->CreateInternetAddr();
	TSharedPtr<FInternetAddr> Destination = SocketSubsystem->CreateInternetAddr();

	uint32 Size = 0;
	int32 NumBytesRead = 0;
	while (Socket->HasPendingData(Size))
	{
		TSharedRef<FArrayReader> Reader = MakeShared<FArrayReader>(true);

		// Use an aligned size instead of ACN_DMX_PACKAGE_SIZE
		constexpr uint32 SACNMaxReaderSize = 1024u;
		Reader->SetNumUninitialized(FMath::Min(Size, SACNMaxReaderSize));

		if (Socket->RecvFromWithPktInfo(Reader->GetData(), Reader->Num(), NumBytesRead, *Sender, *Destination))
		{
			Reader->RemoveAt(NumBytesRead, Reader->Num() - NumBytesRead, false);
			uint32 MulticastIp = 0;
			Destination->GetIp(MulticastIp);

			if (const uint16* UniverseIDPtr = MulticastGroupAddrToUniverseIDMap.Find(MulticastIp))
			{
				const uint16 UniverseID = *UniverseIDPtr;
				DistributeReceivedData(UniverseID, Reader);
			}
		}
	}
}

void FDMXProtocolSACNReceiver::DistributeReceivedData(uint16 UniverseID, const TSharedRef<FArrayReader>& PacketReader)
{
#if UE_BUILD_DEBUG
	check(Protocol.IsValid());
#endif

	switch (GetRootPacketType(PacketReader))
	{
	case VECTOR_ROOT_E131_DATA:
		HandleDataPacket(UniverseID, PacketReader);
		return;
	default:
		return;
	}
}

void FDMXProtocolSACNReceiver::HandleDataPacket(uint16 UniverseID, const TSharedRef<FArrayReader>& PacketReader)
{
	check(Protocol.IsValid());

	FDMXProtocolE131RootLayerPacket IncomingDMXRootLayer;
	FDMXProtocolE131FramingLayerPacket IncomingDMXFramingLayer;
	FDMXProtocolE131DMPLayerPacket IncomingDMXDMPLayer;

	*PacketReader << IncomingDMXRootLayer;
	*PacketReader << IncomingDMXFramingLayer;
	*PacketReader << IncomingDMXDMPLayer;

	// Ignore packets of lower priority than the highest one we received.
	if (HighestReceivedPriority > IncomingDMXFramingLayer.Priority)
	{		return;
	}
	HighestReceivedPriority = IncomingDMXFramingLayer.Priority;

	// Make sure we copy same amount of data

	FDMXSignalSharedRef DMXSignal = MakeShared<FDMXSignal, ESPMode::ThreadSafe>(FPlatformTime::Seconds(), UniverseID, TArray<uint8>(IncomingDMXDMPLayer.DMX, DMX_UNIVERSE_SIZE));
	for (const TSharedPtr<FDMXInputPort, ESPMode::ThreadSafe>& InputPort : AssignedInputPorts)
	{
		InputPort->SingleProducerInputDMXSignal(DMXSignal);
	}
		
	INC_DWORD_STAT(STAT_SACNPackagesReceived);
}

uint32 FDMXProtocolSACNReceiver::GetRootPacketType(const TSharedPtr<FArrayReader>& Buffer)
{
	uint32 Vector = 0x00000000;
	const uint32 MinCheck = ACN_ADDRESS_ROOT_VECTOR + 4;
	if (Buffer->Num() > MinCheck)
	{
		// Get OpCode
		Buffer->Seek(ACN_ADDRESS_ROOT_VECTOR);
		*Buffer << Vector;
		Buffer->ByteSwap(&Vector, 4);

		// Reset Position
		Buffer->Seek(0);
	}

	return Vector;
}

uint32 FDMXProtocolSACNReceiver::GetIpForUniverseID(uint16 InUniverseID)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedPtr<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();
	uint32 ReturnAddress = 0;
	FBufferArchive IP;
	uint8 IP_0 = ACN_UNIVERSE_IP_0;
	uint8 IP_1 = ACN_UNIVERSE_IP_1;
	IP << IP_0; // [x.?.?.?]
	IP << IP_1; // [x.x.?.?]
	IP.ByteSwap(&InUniverseID, sizeof(uint16));
	IP << InUniverseID;	// [x.x.x.x]

	InternetAddr->SetRawIp(IP);
	InternetAddr->GetIp(ReturnAddress);
	return ReturnAddress;
}
