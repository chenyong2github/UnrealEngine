// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACN.h"

#include "DMXProtocolBlueprintLibrary.h"
#include "DMXProtocolTransportSACN.h"
#include "DMXProtocolSACNReceivingRunnable.h"
#include "Common/UdpSocketBuilder.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"

#include "Packets/DMXProtocolE131PDUPacket.h"
#include "DMXProtocolPackager.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolUniverseSACN.h"
#include "Managers/DMXProtocolUniverseManager.h"
#include "DMXStats.h"

#include "Serialization/BufferArchive.h"

// Stats
DECLARE_CYCLE_STAT(TEXT("SACN Packages Enqueue To Send"), STAT_SACNPackagesEnqueueToSend, STATGROUP_DMX);

FDMXProtocolSACN::FDMXProtocolSACN(const FName& InProtocolName, FJsonObject& InSettings)
	: bShouldSendDMX(false)
	, bShouldReceiveDMX(false)
	, ProtocolName(InProtocolName)
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
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();

	InterfaceIPAddress = ProtocolSettings->InterfaceIPAddress;
	bShouldSendDMX = ProtocolSettings->IsSendDMXEnabled();
	bShouldReceiveDMX = ProtocolSettings->IsReceiveDMXEnabled();

	ReceivingRunnable = FDMXProtocolSACNReceivingRunnable::CreateNew(ProtocolSettings->ReceivingRefreshRate);

	// Set Delegates
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

	return true;
}

bool FDMXProtocolSACN::IsEnabled() const
{
	return true;
}

void FDMXProtocolSACN::SetSendDMXEnabled(bool bEnabled)
{
	bShouldSendDMX = true;
}

bool FDMXProtocolSACN::IsSendDMXEnabled() const
{
	return bShouldSendDMX;
}

void FDMXProtocolSACN::SetReceiveDMXEnabled(bool bEnabled)
{
	bShouldReceiveDMX = bEnabled;

	if (bShouldReceiveDMX)
	{
		CreateDMXListenersInUniverses();
	}
	else
	{
		DestroyDMXListenersInUniverses();
	}
}

bool FDMXProtocolSACN::IsReceiveDMXEnabled() const
{
	return bShouldReceiveDMX;
}

TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> FDMXProtocolSACN::AddUniverse(const FJsonObject& InSettings)
{
	checkf(InSettings.HasField(DMXJsonFieldNames::DMXUniverseID), TEXT("DMXProtocol UniverseID is not valid"));
	uint32 UniverseID = InSettings.GetNumberField(DMXJsonFieldNames::DMXUniverseID);
	TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(UniverseID);
	if (Universe.IsValid())
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("Universe %i exist"), UniverseID);
		return Universe;
	}

	Universe = MakeShared<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe>(SharedThis(this), InSettings);

	// Start listening instantly if we should receive DMX
	if (bShouldReceiveDMX)
	{
		Universe->CreateDMXListener();
	}

	return UniverseManager->AddUniverse(Universe->GetUniverseID(), Universe);
}

void FDMXProtocolSACN::CollectUniverses(const TArray<FDMXCommunicationEndpoint>& Endpoints)
{
	for (const FDMXCommunicationEndpoint& Endpoint : Endpoints)
	{
		FJsonObject UniverseSettings;
		UniverseSettings.SetNumberField(DMXJsonFieldNames::DMXUniverseID, Endpoint.UniverseNumber);
		
		TArray<TSharedPtr<FJsonValue>> IpAddresses;
		if (Endpoint.ShouldBroadcastOnly()) 
		{
			IpAddresses.Add(MakeShared<FJsonValueNumber>(GetUniverseAddrByID(Endpoint.UniverseNumber)));
		}
		else
		{
			for (FString IpAddress : Endpoint.UnicastIpAddresses)
			{
				if (IpAddress.IsEmpty())
				{
					IpAddresses.Add(MakeShared<FJsonValueNumber>(GetUniverseAddrByID(Endpoint.UniverseNumber)));
				}
				else
				{
					IpAddresses.Add(MakeShared<FJsonValueNumber>(GetUniverseAddrUnicast(IpAddress)));
				}
			}
		}
		
		UniverseSettings.SetArrayField(DMXJsonFieldNames::DMXIpAddresses, IpAddresses);
		UniverseSettings.SetNumberField(DMXJsonFieldNames::DMXEthernetPort, ACN_PORT);

		if (UniverseManager->GetAllUniverses().Contains(Endpoint.UniverseNumber))
		{
			UpdateUniverse(Endpoint.UniverseNumber, UniverseSettings);
			continue;
		}

		AddUniverse(UniverseSettings);
	}
}

void FDMXProtocolSACN::UpdateUniverse(uint32 InUniverseId, const FJsonObject& InSettings)
{
	TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(InUniverseId);
	if (Universe.IsValid())
	{
		Universe->UpdateSettings(InSettings);
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

void FDMXProtocolSACN::GetDefaultUniverseSettings(uint16 InUniverseID, FJsonObject& OutSettings) const
{
	OutSettings.SetNumberField(DMXJsonFieldNames::DMXPortID, 0.0);
	OutSettings.SetNumberField(DMXJsonFieldNames::DMXUniverseID, InUniverseID);
	OutSettings.SetNumberField(DMXJsonFieldNames::DMXEthernetPort, ACN_PORT);
	const TArray<TSharedPtr<FJsonValue>> IpAddresses = { MakeShared<FJsonValueNumber>(GetUniverseAddrByID(InUniverseID)) };
	OutSettings.SetArrayField(DMXJsonFieldNames::DMXIpAddresses, IpAddresses); // Broadcast IP address
}

void FDMXProtocolSACN::ClearInputBuffers()
{
	if (ReceivingRunnable.IsValid())
	{
		ReceivingRunnable->ClearBuffers();
	}
}

void FDMXProtocolSACN::ZeroOutputBuffers()
{
	for (const TPair<uint32, TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe>>& UniverseIDUniverseKvP : UniverseManager->GetAllUniverses())
	{
		UniverseIDUniverseKvP.Value->ZeroOutputDMXBuffer();
	}
}

const FName& FDMXProtocolSACN::GetProtocolName() const
{
	return ProtocolName;
}

const IDMXUniverseSignalMap& FDMXProtocolSACN::GameThreadGetInboundSignals() const
{
	if (ReceivingRunnable.IsValid())
	{
		return ReceivingRunnable->GameThread_GetInputBuffer();
	}

	return EmptyBufferDummy;
}

TSharedPtr<IDMXProtocolSender> FDMXProtocolSACN::GetSenderInterface() const
{
	return SACNSender;
}

bool FDMXProtocolSACN::Tick(float DeltaTime)
{
	if (bShouldReceiveDMX)
	{
		const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();

		for (const TPair<uint32, FDMXProtocolUniverseSACNPtr>& UniversePair : UniverseManager->GetAllUniverses())
		{
			if (FDMXProtocolUniverseSACNPtr Universe = UniversePair.Value)
			{
				Universe->Tick(DeltaTime);
			}
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
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(PacketSettings, ACN_MAX_UNIVERSES, Packager.GetBuffer());
	GetSenderInterface()->EnqueueOutboundPackage(Packet);

	return true;
}

EDMXSendResult FDMXProtocolSACN::InputDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment)
{
	uint16 FinalInputUniverseID = GetFinalSendUniverseID(UniverseID);

	if (ReceivingRunnable.IsValid() && IsInGameThread())
	{
		ReceivingRunnable->GameThread_InputDMXFragment(UniverseID, DMXFragment);
	}
	else
	{
		// Only implemented for game thread
		checkNoEntry();
	}

	return EDMXSendResult::Success;
}

EDMXSendResult FDMXProtocolSACN::SendDMXFragment(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment)
{
	if (!bShouldSendDMX)
	{
		// We successfully completed the send command by not sending, when globally dmx shouldn't be sent
		return EDMXSendResult::Success;
	}

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

	if (Result == EDMXSendResult::Success)
	{
		const FDMXBufferPtr& OutputBuffer = Universe->GetOutputDMXBuffer();
		check(OutputBuffer.IsValid());

		OutputBuffer->AccessDMXData([this, FinalSendUniverseID](TArray<uint8>& Buffer) {
			OnUniverseOutputBufferUpdated.Broadcast(ProtocolName, FinalSendUniverseID, Buffer);
			});
	}

	return Result;
}

EDMXSendResult FDMXProtocolSACN::SendDMXFragmentCreate(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment)
{
	if (!bShouldSendDMX)
	{
		// We successfully completed the send command by not sending, when globally dmx shouldn't be sent
		return EDMXSendResult::Success;
	}

	uint16 FinalSendUniverseID = GetFinalSendUniverseID(InUniverseID);

	// Lock the universe if we use separate thread
	TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe> Universe = UniverseManager->AddUniverseCreate(FinalSendUniverseID);

	// Start listening instantly if we should receive DMX
	if (bShouldReceiveDMX)
	{
		Universe->CreateDMXListener();
	}

	if (!Universe->SetDMXFragment(DMXFragment))
	{
		return EDMXSendResult::ErrorSetBuffer;
	}

	EDMXSendResult Result = SendDMXInternal(FinalSendUniverseID, Universe->GetOutputDMXBuffer());
	return Result;
}

EDMXSendResult FDMXProtocolSACN::SendDMXZeroUniverse(uint16 InUniverseID, bool bForceSendDMX /** = false */)
{
	if (!bShouldSendDMX)
	{
		// We successfully completed the send command by not sending, when globally dmx shouldn't be sent
		return EDMXSendResult::Success;
	}

	uint16 FinalSendUniverseID = GetFinalSendUniverseID(InUniverseID);

	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(FinalSendUniverseID);
	if (!Universe.IsValid())
	{
		return EDMXSendResult::ErrorGetUniverse;
	}

	Universe->ZeroOutputDMXBuffer();

	if (bForceSendDMX)
	{
		return SendDMXInternal(FinalSendUniverseID, Universe->GetOutputDMXBuffer());
	}

	return EDMXSendResult::Success;
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
	check(bShouldSendDMX);

	// Init Packager
	FDMXProtocolPackager Packager;

	// SACN PDU packets
	FDMXProtocolE131RootLayerPacket RootLayer;
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
	PacketSettings.SetNumberField(TEXT("UniverseID"), InUniverseID);
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(PacketSettings, InUniverseID, Packager.GetBuffer());
	TSharedPtr<IDMXProtocolSender> SenderInterface = GetSenderInterface();
	// @TODO: Check why sender interface could be invalid
	if (SenderInterface.IsValid())
	{
		if (!SenderInterface->EnqueueOutboundPackage(Packet))
		{
			return EDMXSendResult::ErrorEnqueuePackage;
		}
	}
	else
	{
		return EDMXSendResult::ErrorNoSenderInterface;
	}

	// Stats
	SCOPE_CYCLE_COUNTER(STAT_SACNPackagesEnqueueToSend);

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

	// Depending on settings, create a receiver with its own thread
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	check(ProtocolSettings);

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

	if (bShouldReceiveDMX)
	{
		CreateDMXListenersInUniverses();
	}

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

void FDMXProtocolSACN::CreateDMXListenersInUniverses()
{
	DestroyDMXListenersInUniverses();

	bShouldReceiveDMX = true;

	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();

	// In any case the universe should listen for incoming DMX. Either the receiver polls its update, or this via Tick.
	check(UniverseManager.IsValid());
	for (const TPair<uint32, FDMXProtocolUniverseSACNPtr>& UniverseKvp : UniverseManager->GetAllUniverses())
	{
		const FDMXProtocolUniverseSACNPtr& Universe = UniverseKvp.Value;
		check(Universe.IsValid());

		Universe->CreateDMXListener();
	}
}

void FDMXProtocolSACN::DestroyDMXListenersInUniverses()
{
	bShouldReceiveDMX = false;

	check(UniverseManager.IsValid());

	// In any case the universe should stop listening for incoming DMX. The receiver only polled its updates.
	for (const TPair<uint32, FDMXProtocolUniverseSACNPtr>& UniverseKvp : UniverseManager->GetAllUniverses())
	{
		const FDMXProtocolUniverseSACNPtr& Universe = UniverseKvp.Value;
		check(Universe.IsValid());

		Universe->DestroyDMXListener();
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

uint32 FDMXProtocolSACN::GetUniverseAddrByID(uint16 InUniverseID)
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

uint32 FDMXProtocolSACN::GetUniverseAddrUnicast(FString UnicastAddress)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedPtr<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();
	uint32 ReturnAddress = 0;
	bool bIsValid = false;
	InternetAddr->SetIp(*UnicastAddress, bIsValid);
	InternetAddr->GetIp(ReturnAddress);

	if (!bIsValid)
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Failed to create unicast address"));
	}

	return ReturnAddress;
}

