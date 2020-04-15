// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACN.h"
#include "DMXProtocolTransportSACN.h"
#include "Common/UdpSocketBuilder.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"

#include "Packets/DMXProtocolE131PDUPacket.h"
#include "DMXProtocolPackager.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolUniverseSACN.h"
#include "Managers/DMXProtocolUniverseManager.h"

#include "Serialization/BufferArchive.h"

FDMXProtocolSACN::FDMXProtocolSACN(const FName& InProtocolName, FJsonObject& InSettings)
	: ProtocolName(InProtocolName)
	, SenderSocket(nullptr)
{
	NetworkErrorMessagePrefix = TEXT("NETWORK ERROR SACN:");

	Settings = MakeShared<FJsonObject>(InSettings);
	UniverseManager = MakeShared<FDMXProtocolUniverseManager<FDMXProtocolUniverseSACN>>(this);
}


TSharedPtr<FJsonObject> FDMXProtocolSACN::GetSettings() const
{
	return Settings;
}

bool FDMXProtocolSACN::Init()
{	
	InterfaceIPAddress = GetDefault<UDMXProtocolSettings>()->InterfaceIPAddress;

	// Set Network Interface listener
	NetworkInterfaceChangedDelegate = FOnNetworkInterfaceChangedDelegate::CreateRaw(this, &FDMXProtocolSACN::OnNetworkInterfaceChanged);
	IDMXProtocol::OnNetworkInterfaceChanged.Add(NetworkInterfaceChangedDelegate);

	// Set Network Interface
	FString ErrorMessage;
	if (!RestartNetworkInterface(InterfaceIPAddress, ErrorMessage))
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:%s"), NetworkErrorMessagePrefix, *ErrorMessage);
	}

	// Declare Engine ticker
	OnEndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FDMXProtocolSACN::OnEndFrame);

	return true;
}

bool FDMXProtocolSACN::Shutdown()
{
	ReleaseNetworkInterface();
	IDMXProtocol::OnNetworkInterfaceChanged.Remove(NetworkInterfaceChangedDelegate.GetHandle());

	FCoreDelegates::OnEndFrame.Remove(OnEndFrameHandle);

	return false;
}

bool FDMXProtocolSACN::IsEnabled() const
{
	return true;
}

TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> FDMXProtocolSACN::AddUniverse(const FJsonObject& InSettings)
{
	TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe> Universe = MakeShared<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe>(SharedThis(this), InSettings);
	return UniverseManager->AddUniverse(Universe->GetUniverseID(), Universe);
}

void FDMXProtocolSACN::CollectUniverses(const TArray<FDMXUniverse>& Universes)
{
	for (const FDMXUniverse& Universe : Universes)
	{
		if (UniverseManager->GetAllUniverses().Contains(Universe.UniverseNumber))
		{
			continue;
		}

		FJsonObject UniverseSettings;
		UniverseSettings.SetNumberField(TEXT("UniverseID"), Universe.UniverseNumber);
		AddUniverse(UniverseSettings);
	}
}

bool FDMXProtocolSACN::RemoveUniverseById(uint32 InUniverseId)
{
	return UniverseManager->RemoveUniverseById(InUniverseId);
}

void FDMXProtocolSACN::RemoveAllUniverses()
{
	UniverseManager->RemoveAll();
}

TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> FDMXProtocolSACN::GetUniverseById(uint32 InUniverseId) const
{
	return UniverseManager->GetUniverseById(InUniverseId);
}


const FName& FDMXProtocolSACN::GetProtocolName() const
{
	return ProtocolName;
}


TSharedPtr<IDMXProtocolSender> FDMXProtocolSACN::GetSenderInterface() const
{
	return SACNSender;
}

bool FDMXProtocolSACN::Tick(float DeltaTime)
{
	for (const TPair<uint32, FDMXProtocolUniverseSACNPtr>& UniversePair : UniverseManager->GetAllUniverses())
	{
		if (FDMXProtocolUniverseSACNPtr Universe = UniversePair.Value)
		{
			Universe->Tick(DeltaTime);
		}
	}

	return true;
}

void FDMXProtocolSACN::OnEndFrame()
{
	Tick(FApp::GetDeltaTime());
}

bool FDMXProtocolSACN::SendDiscovery(const TArray<uint16>& Universes)
{
	// Init Packager
	FDMXProtocolPackager Packager;

	FDMXProtocolE131RootLayerPacket RootLayer;
	static FGuid Guid = FGuid::NewGuid();
	FMemory::Memcpy(RootLayer.CID, &Guid, ACN_CIDBYTES);

	FDMXProtocolUDPE131FramingLayerPacket FramingLayer;
	FDMXProtocolUDPE131DiscoveryLayerPacket DiscoveryLayer;
	if (Universes.Num() == ACN_DMX_SIZE)
	{
		FMemory::Memcpy(DiscoveryLayer.Universes, Universes.GetData(), ACN_DMX_SIZE);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:Size of outcoming sACN DMX discovery buffer is wrong"), NetworkErrorMessagePrefix);
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

EDMXSendResult FDMXProtocolSACN::SendDMXFragment(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment)
{
	uint16 FinalSendUniverseID = GetFinalSendUniverseID(InUniverseID);

	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(FinalSendUniverseID);
	if (!Universe.IsValid())
	{
		return EDMXSendResult::ErrorGetUniverse;
	}

	if (!Universe->SetDMXFragment(DMXFragment))
	{
		return EDMXSendResult::ErrorSetBuffer;
	}

	EDMXSendResult Result = SendDMXInternal(FinalSendUniverseID, Universe->GetOutputDMXBuffer());
	return Result;
}

EDMXSendResult FDMXProtocolSACN::SendDMXFragmentCreate(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment)
{
	uint16 FinalSendUniverseID = GetFinalSendUniverseID(InUniverseID);

	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = UniverseManager->AddUniverseCreate(FinalSendUniverseID);

	if (!Universe->SetDMXFragment(DMXFragment))
	{
		return EDMXSendResult::ErrorSetBuffer;
	}

	EDMXSendResult Result = SendDMXInternal(FinalSendUniverseID, Universe->GetOutputDMXBuffer());
	return Result;
}

uint16 FDMXProtocolSACN::GetFinalSendUniverseID(uint16 InUniverseID) const
{
	// GlobalSACNUniverseOffset clamp between 0 and 65535(max uint16) 
	return InUniverseID + GetDefault<UDMXProtocolSettings>()->GlobalSACNUniverseOffset;
}

uint32 FDMXProtocolSACN::GetUniversesNum() const
{
	return UniverseManager->GetAllUniverses().Num();
}

uint16 FDMXProtocolSACN::GetMinUniverseID() const
{
	return 1;
}

uint16 FDMXProtocolSACN::GetMaxUniverses() const
{
	return ACN_MAX_UNIVERSES;
}

EDMXSendResult FDMXProtocolSACN::SendDMXInternal(uint16 InUniverseID, const FDMXBufferPtr& DMXBuffer) const
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// SACN PDU packets
	FDMXProtocolE131RootLayerPacket RootLayer;;
	static FGuid Guid = FGuid::NewGuid();
	FMemory::Memcpy(RootLayer.CID, &Guid, ACN_CIDBYTES);

	FDMXProtocolE131FramingLayerPacket FramingLayer;
	// To detect duplicate or out of order packets. This is using on physical controllers
	static uint8 SequenceNumber = 0;
	FramingLayer.SequenceNumber = SequenceNumber++;
	FramingLayer.Universe = InUniverseID;

	FDMXProtocolE131DMPLayerPacket DMPLayer;
	DMPLayer.AddressIncrement = ACN_ADDRESS_INC;
	DMPLayer.PropertyValueCount = ACN_DMX_SIZE + 1;
	bool bBufferSizeIsWrong = false;
	DMXBuffer->AccessDMXData([&DMPLayer, &bBufferSizeIsWrong](TArray<uint8>& InData)
		{
			if (InData.Num() == ACN_DMX_SIZE)
			{
				FMemory::Memcpy(DMPLayer.DMX, InData.GetData(), ACN_DMX_SIZE);
			}
			else
			{
				bBufferSizeIsWrong = true;
			}
		});

	if (bBufferSizeIsWrong)
	{
		return EDMXSendResult::ErrorSizeBuffer;
	}

	Packager.AddToPackage(&RootLayer);
	Packager.AddToPackage(&FramingLayer);
	Packager.AddToPackage(&DMPLayer);

	// Sending
	FJsonObject PacketSettings;
	PacketSettings.SetNumberField("UniverseID", InUniverseID);
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(PacketSettings, Packager.GetBuffer());

	if (!GetSenderInterface()->EnqueueOutboundPackage(Packet))
	{
		return EDMXSendResult::ErrorEnqueuePackage;
	}

	return EDMXSendResult::Success;
}

void FDMXProtocolSACN::OnNetworkInterfaceChanged(const FString& InInterfaceIPAddress)
{
	FString ErrorMessage;
	if (!RestartNetworkInterface(InInterfaceIPAddress, ErrorMessage))
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:%s"), NetworkErrorMessagePrefix, *ErrorMessage);
	}
}

bool FDMXProtocolSACN::RestartNetworkInterface(const FString& InInterfaceIPAddress, FString& OutErrorMessage)
{
	FScopeLock Lock(&SenderSocketCS);

	// Clean the error message
	OutErrorMessage.Empty();

	// Try to create IP address at the first
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	// Create sender address
	TSharedPtr<FInternetAddr> SenderAddr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	SenderAddr->SetIp(*InInterfaceIPAddress, bIsValid);
	SenderAddr->SetPort(ACN_PORT);
	if (!bIsValid)
	{
		OutErrorMessage = FString::Printf(TEXT("Wrong IP address: %s"), *InInterfaceIPAddress);
		return false;
	}

	// Release old network interface
	ReleaseNetworkInterface();

	FIPv4Endpoint SenderEndpoint = FIPv4Endpoint(SenderAddr);

	// Create sender socket
	FSocket* NewSenderSocket = FUdpSocketBuilder(TEXT("UDPSACNSenderSocket"))
		.AsNonBlocking()
#if PLATFORM_SUPPORTS_UDP_MULTICAST_GROUP
        .WithMulticastInterface(SenderEndpoint.Address)
        .WithMulticastLoopback()
        .WithMulticastTtl(1)
#endif
        .BoundToEndpoint(SenderEndpoint)
		.AsReusable();

	if (NewSenderSocket == nullptr)
	{
		OutErrorMessage = FString::Printf(TEXT("Error create SenderSocket: %s"), *InterfaceIPAddress);
		return false;
	}

	// Set new network interface IP
	InterfaceIPAddress = InInterfaceIPAddress;

	// Save New socket;
	SenderSocket = NewSenderSocket;

	SACNSender = MakeShared<FDMXProtocolSenderSACN>(*SenderSocket, this);

	return true;
}

void FDMXProtocolSACN::ReleaseNetworkInterface()
{
	if (SACNSender)
	{
		SACNSender.Reset();
		SACNSender = nullptr;
	}

	// Clean all sockets!
	if (SenderSocket != nullptr)
	{
		SenderSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SenderSocket);
		SenderSocket = nullptr;
	}
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
	IP.ByteSwap(&InUniverseID, sizeof(uint16));
	IP << InUniverseID;	// [x.x.x.x]

	InternetAddr->SetRawIp(IP);

	return InternetAddr;
}