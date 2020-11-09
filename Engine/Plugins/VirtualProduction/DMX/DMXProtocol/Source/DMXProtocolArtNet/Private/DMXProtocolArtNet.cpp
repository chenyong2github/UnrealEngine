// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolArtNet.h"

#include "DMXProtocolBlueprintLibrary.h"
#include "DMXProtocolTransportArtNet.h"
#include "Common/UdpSocketBuilder.h"

#include "DMXProtocolSettings.h"
#include "Managers/DMXProtocolUniverseManager.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolPackager.h"

#include "DMXProtocolArtNetUtils.h"
#include "DMXProtocolUniverseArtNet.h"
#include "DMXProtocolReceivingRunnable.h"

#include "Managers/DMXProtocolUniverseManager.h"

#include "DMXStats.h"

// Stats
DECLARE_CYCLE_STAT(TEXT("Art-Net Packages Enqueue To Send"), STAT_ArtNetPackagesEnqueueToSend, STATGROUP_DMX);

using namespace ArtNet;


FDMXProtocolArtNet::FDMXProtocolArtNet(const FName& InProtocolName, const FJsonObject& InSettings)
	: bShouldSendDMX(false)
	, bShouldReceiveDMX(false)
	, ProtocolName(InProtocolName)	
	, BroadcastSocket(nullptr)
	, ListeningSocket(nullptr)
	, bUseSeparateReceivingThread(true)
{
	NetworkErrorMessagePrefix = TEXT("NETWORK ERROR Art-Net:");

	Settings = MakeShared<FJsonObject>(InSettings);
	UniverseManager = MakeShared<FDMXProtocolUniverseManager<FDMXProtocolUniverseArtNet>>(this);
}

TSharedPtr<IDMXProtocolSender> FDMXProtocolArtNet::GetSenderInterface() const
{
	return ArtNetSender;
}

bool FDMXProtocolArtNet::Init()
{
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();

	InterfaceIPAddress = ProtocolSettings->InterfaceIPAddress;
	bShouldSendDMX = ProtocolSettings->IsSendDMXEnabled();
	bShouldReceiveDMX = ProtocolSettings->IsReceiveDMXEnabled();

	// Set Network Interface listener
	NetworkInterfaceChangedHandle = IDMXProtocol::OnNetworkInterfaceChanged.AddRaw(this, &FDMXProtocolArtNet::OnNetworkInterfaceChanged);

	// Set Network Interface
	FString ErrorMessage;
	if (!RestartNetworkInterface(InterfaceIPAddress, ErrorMessage))
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:%s"), NetworkErrorMessagePrefix, *ErrorMessage);
	}
	
	return true;
}

bool FDMXProtocolArtNet::Shutdown()
{
	ReleaseArtNetReceiver();

	ReleaseNetworkInterface();
	IDMXProtocol::OnNetworkInterfaceChanged.Remove(NetworkInterfaceChangedHandle);

	return true;
}

bool FDMXProtocolArtNet::IsEnabled() const
{
	return true;
}

void FDMXProtocolArtNet::SetSendDMXEnabled(bool bEnabled)
{
	bShouldSendDMX = true;
}

bool FDMXProtocolArtNet::IsSendDMXEnabled() const
{
	return bShouldSendDMX;
}

void FDMXProtocolArtNet::SetReceiveDMXEnabled(bool bEnabled)
{
	bShouldReceiveDMX = bEnabled;

	if (bShouldReceiveDMX)
	{
		FString CreateDMXListenerError;
		if (!CreateDMXListener(CreateDMXListenerError))
		{
			UE_LOG_DMXPROTOCOL(Error, TEXT("%s"), *CreateDMXListenerError);
		}
	}
	else
	{
		DestroyDMXListener();
	}
}

bool FDMXProtocolArtNet::IsReceiveDMXEnabled() const
{
	return bShouldReceiveDMX;
}

TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> FDMXProtocolArtNet::AddUniverse(const FJsonObject& InSettings)
{
	checkf(InSettings.HasField(DMXJsonFieldNames::DMXUniverseID), TEXT("DMXProtocol UniverseID is not valid"));
	uint32 UniverseID = InSettings.GetNumberField(DMXJsonFieldNames::DMXUniverseID);
	TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(UniverseID);
	if (Universe.IsValid())
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("Universe %i exist"), UniverseID);
		return Universe;
	}

	Universe = MakeShared<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe>(SharedThis(this), InSettings);

	return UniverseManager->AddUniverse(Universe->GetUniverseID(), Universe);
}

void FDMXProtocolArtNet::CollectUniverses(const TArray<FDMXCommunicationEndpoint>& Endpoints)
{
	for (const FDMXCommunicationEndpoint& Endpoint : Endpoints)
	{
		FJsonObject UniverseSettings;
		UniverseSettings.SetNumberField(DMXJsonFieldNames::DMXUniverseID, Endpoint.UniverseNumber);
		UniverseSettings.SetNumberField(DMXJsonFieldNames::DMXPortID, 0); // For now use port 0 for ArtNet
		
		TArray<TSharedPtr<FJsonValue>> IpAddresses;
		if (Endpoint.ShouldBroadcastOnly()) 
		{
			IpAddresses.Add(MakeShared<FJsonValueNumber>(GetUniverseAddr("")));
		}
		else
		{
			for (FString IpAddress : Endpoint.UnicastIpAddresses)
			{
				IpAddresses.Add(MakeShared<FJsonValueNumber>(GetUniverseAddr(IpAddress)));
			}
		}
		
		UniverseSettings.SetArrayField(DMXJsonFieldNames::DMXIpAddresses, IpAddresses);
		UniverseSettings.SetNumberField(DMXJsonFieldNames::DMXEthernetPort, ARTNET_PORT);
		if (UniverseManager->GetAllUniverses().Contains(Endpoint.UniverseNumber))
		{
			UpdateUniverse(Endpoint.UniverseNumber, UniverseSettings);
			continue;
		}

		AddUniverse(UniverseSettings);
	}
}

void FDMXProtocolArtNet::GetDefaultUniverseSettings(uint16 InUniverseID, FJsonObject& OutSettings) const
{
	OutSettings.SetNumberField(DMXJsonFieldNames::DMXPortID, 0.0);
	OutSettings.SetNumberField(DMXJsonFieldNames::DMXUniverseID, InUniverseID);
	OutSettings.SetNumberField(DMXJsonFieldNames::DMXEthernetPort, ARTNET_PORT);
	const TArray<TSharedPtr<FJsonValue>> IpAddresses = { MakeShared<FJsonValueNumber>(GetUniverseAddr(FString())) };
	OutSettings.SetArrayField(DMXJsonFieldNames::DMXIpAddresses, IpAddresses); // Broadcast IP address
}

void FDMXProtocolArtNet::ClearInputBuffers()
{
	if (ReceivingRunnable.IsValid())
	{
		ReceivingRunnable->ClearBuffers();
	}
}

void FDMXProtocolArtNet::ZeroOutputBuffers()
{
	for (const TPair<uint32, TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe>>& UniverseIDUniverseKvP : UniverseManager->GetAllUniverses())
	{
		UniverseIDUniverseKvP.Value->ZeroOutputDMXBuffer();
	}
}

void FDMXProtocolArtNet::UpdateUniverse(uint32 InUniverseId, const FJsonObject& InSettings)
{
	TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(InUniverseId);
	if (Universe.IsValid())
	{
		Universe->UpdateSettings(InSettings);
	}
}

bool FDMXProtocolArtNet::RemoveUniverseById(uint32 InUniverseId)
{
	return UniverseManager->RemoveUniverseById(InUniverseId);
}

void FDMXProtocolArtNet::RemoveAllUniverses()
{
	UniverseManager->RemoveAll();
}

TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> FDMXProtocolArtNet::GetUniverseById(uint32 InUniverseId) const
{
	return UniverseManager->GetUniverseById(InUniverseId);
}

uint32 FDMXProtocolArtNet::GetUniversesNum() const
{
	return UniverseManager->GetAllUniverses().Num();
}

uint16 FDMXProtocolArtNet::GetMinUniverseID() const
{
	return 0;
}

uint16 FDMXProtocolArtNet::GetMaxUniverses() const
{
	return ARTNET_MAX_UNIVERSES;
}

void FDMXProtocolArtNet::OnNetworkInterfaceChanged(const FString& InInterfaceIPAddress)
{
	FString ErrorMessage;
	if (!RestartNetworkInterface(InInterfaceIPAddress, ErrorMessage))
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:%s"), NetworkErrorMessagePrefix, *ErrorMessage);
	}
}

bool FDMXProtocolArtNet::RestartNetworkInterface(const FString& InInterfaceIPAddress, FString& OutErrorMessage)
{
	// Depending on settings, create a receiver with its own thread
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	check(ProtocolSettings);

	ReceivingRunnable = FDMXProtocolArtNetReceivingRunnable::CreateNew(ProtocolSettings->ReceivingRefreshRate);

	FScopeLock Lock(&SocketsCS);

	// Clean the error message
	OutErrorMessage.Empty();

	// Try to create IP address at the first
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	// Create sender address
	SenderAddr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	SenderAddr->SetIp(*InInterfaceIPAddress, bIsValid);
	SenderAddr->SetPort(ARTNET_SENDER_PORT);
	SenderEndpoint = FIPv4Endpoint(SenderAddr);
	if (!bIsValid)
	{
		OutErrorMessage = FString::Printf(TEXT("Wrong IP address: %s"), *InInterfaceIPAddress);
		return false;
	}

	// Release old network interface
	ReleaseNetworkInterface();

	BroadcastAddr = SocketSubsystem->CreateInternetAddr();

	BroadcastAddr->SetBroadcastAddress();
	BroadcastAddr->SetPort(ARTNET_PORT);
	BroadcastEndpoint = FIPv4Endpoint(BroadcastAddr);

	FSocket* NewBroadcastSocket = FUdpSocketBuilder(TEXT("UDPArtNetBroadcastSocket"))
		.AsNonBlocking()
		.AsReusable()
		.WithBroadcast()
		.BoundToEndpoint(SenderEndpoint);

	if (NewBroadcastSocket == nullptr)
	{
		OutErrorMessage = FString::Printf(TEXT("Error create BroadcastSocket: %s"), *InterfaceIPAddress);
		return false;
	}

	// Set new network interface IP
	InterfaceIPAddress = InInterfaceIPAddress;

	// Save New socket;
	BroadcastSocket = NewBroadcastSocket;

	// Create sender
	ArtNetSender = MakeShared<FDMXProtocolSenderArtNet>(*BroadcastSocket, this);

	// Create a listener if DMX should be received
	if(bShouldReceiveDMX)
	{
		if (!CreateDMXListener(OutErrorMessage))
		{
			return false;
		}
	}

	return true;
}

void FDMXProtocolArtNet::ReleaseNetworkInterface()
{
	if (ArtNetSender.IsValid())
	{
		ArtNetSender.Reset();
		ArtNetSender = nullptr;
	}

	if (ArtNetReceiver.IsValid())
	{
		ArtNetReceiver.Reset();
		ArtNetReceiver = nullptr;
	}

	// Clean all sockets!
	if (BroadcastSocket != nullptr)
	{
		BroadcastSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(BroadcastSocket);
		BroadcastSocket = nullptr;
	}

	if (ListeningSocket != nullptr)
	{
		ListeningSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListeningSocket);
		ListeningSocket = nullptr;
	}
}

bool FDMXProtocolArtNet::CreateDMXListener(FString& OutErrorMessage)
{
	DestroyDMXListener();

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	// Create Listening socket
	bool bIsValid = false;
	TSharedPtr<FInternetAddr> ListeningAddr = SocketSubsystem->CreateInternetAddr();
	ListeningAddr->SetIp(*InterfaceIPAddress, bIsValid);
	ListeningAddr->SetPort(ARTNET_PORT);
	FIPv4Endpoint ListenerEndpoint = FIPv4Endpoint(ListeningAddr);
	if (!bIsValid)
	{
		OutErrorMessage = FString::Printf(TEXT("Wrong IP address: %s"), *InterfaceIPAddress);
		return false;
	}

	FSocket* NewListeningSocket = FUdpSocketBuilder(TEXT("UDPArtNetListeningSocket"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToEndpoint(ListenerEndpoint);

	if (NewListeningSocket == nullptr)
	{
		OutErrorMessage = FString::Printf(TEXT("Error create ListeningSocket: %s"), *InterfaceIPAddress);
		return false;
	}

	// Save New socket;
	ListeningSocket = NewListeningSocket;

	FTimespan ThreadWaitTime = FTimespan::FromMilliseconds(0);
	ArtNetReceiver = MakeShared<FDMXProtocolReceiverArtNet>(*ListeningSocket, this, ThreadWaitTime);
	ArtNetReceiver->OnDataReceived().BindRaw(this, &FDMXProtocolArtNet::OnDataReceived);

	return true;
}

void FDMXProtocolArtNet::DestroyDMXListener()
{
	if (ArtNetReceiver.IsValid())
	{
		ArtNetReceiver.Reset();
		ArtNetReceiver = nullptr;
	}

	if (ListeningSocket != nullptr)
	{
		ListeningSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListeningSocket);
		ListeningSocket = nullptr;
	}
}

const FName& FDMXProtocolArtNet::GetProtocolName() const
{
	return ProtocolName;
}

const IDMXUniverseSignalMap& FDMXProtocolArtNet::GameThreadGetInboundSignals() const
{
	if (ReceivingRunnable.IsValid())
	{
		return ReceivingRunnable->GameThread_GetInputBuffer();
	}

	return EmptyBufferDummy;
}

TSharedPtr<FJsonObject> FDMXProtocolArtNet::GetSettings() const
{
	return Settings;
}

EDMXSendResult FDMXProtocolArtNet::InputDMXFragment(uint16 UniverseID, const IDMXFragmentMap& DMXFragment)
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

EDMXSendResult FDMXProtocolArtNet::SendDMXFragment(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment)
{
	if (!bShouldSendDMX)
	{
		// We successfully completed the send command by not sending, when globally dmx shouldn't be sent
		return EDMXSendResult::Success;
	}

	uint16 FinalSendUniverseID = GetFinalSendUniverseID(InUniverseID);

	TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(FinalSendUniverseID);
	if (!Universe.IsValid())
	{
		return EDMXSendResult::ErrorGetUniverse;
	}

	if (!Universe->SetDMXFragment(DMXFragment))
	{
		return EDMXSendResult::ErrorSetBuffer;
	}

	EDMXSendResult Result = SendDMXInternal(FinalSendUniverseID, Universe->GetPortID(), Universe->GetOutputDMXBuffer());
	
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

EDMXSendResult FDMXProtocolArtNet::SendDMXFragmentCreate(uint16 InUniverseID, const IDMXFragmentMap& DMXFragment)
{
	if (!bShouldSendDMX)
	{
		// We successfully completed the send command by not sending, when globally dmx shouldn't be sent
		return EDMXSendResult::Success;
	}

	uint16 FinalSendUniverseID = GetFinalSendUniverseID(InUniverseID);

	TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = UniverseManager->AddUniverseCreate(FinalSendUniverseID);

	if (!Universe->SetDMXFragment(DMXFragment))
	{
		return EDMXSendResult::ErrorSetBuffer;
	}

	EDMXSendResult Result = SendDMXInternal(FinalSendUniverseID, Universe->GetPortID(), Universe->GetOutputDMXBuffer());
	return Result;
}

EDMXSendResult FDMXProtocolArtNet::SendDMXZeroUniverse(uint16 InUniverseID, bool bForceSendDMX /** = false */)
{
	if (!bShouldSendDMX)
	{
		// We successfully completed the send command by not sending, when globally dmx shouldn't be sent.
		// Note, we do ignore bForceSendDMX here to give precendence to the global setting over all subsequent
		return EDMXSendResult::Success;
	}

	uint16 FinalSendUniverseID = GetFinalSendUniverseID(InUniverseID);

	TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(FinalSendUniverseID);
	if (!Universe.IsValid())
	{
		return EDMXSendResult::ErrorGetUniverse;
	}

	Universe->ZeroOutputDMXBuffer();

	if (bForceSendDMX)
	{
		return SendDMXInternal(FinalSendUniverseID, Universe->GetPortID(), Universe->GetOutputDMXBuffer());
	}

	return EDMXSendResult::Success;
}

uint16 FDMXProtocolArtNet::GetFinalSendUniverseID(uint16 InUniverseID) const
{
	// GlobalArtNetUniverseOffset clamp between 0 and 65535(max uint16) 
	return InUniverseID + GetDefault<UDMXProtocolSettings>()->GlobalArtNetUniverseOffset;
}

bool FDMXProtocolArtNet::Tick(float DeltaTime)
{
	return true;
}

EDMXSendResult FDMXProtocolArtNet::SendDMXInternal(uint16 UniverseID, uint8 PortID, const FDMXBufferPtr& DMXBuffer) const
{
	check(bShouldSendDMX);

	// Init Packager
	FDMXProtocolPackager Packager;

	// ArtNet DMX packet
	FDMXProtocolArtNetDMXPacket ArtNetDMXPacket;
	bool bBufferSizeIsWrong = false;
	DMXBuffer->AccessDMXData([&ArtNetDMXPacket, &bBufferSizeIsWrong](TArray<uint8>& InData)
	{
		if (InData.Num() == ARTNET_DMX_LENGTH)
		{
			FMemory::Memcpy(ArtNetDMXPacket.Data, InData.GetData(), ARTNET_DMX_LENGTH);
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

	//Set Packet Data
	ArtNetDMXPacket.Physical = PortID;
	ArtNetDMXPacket.Universe = UniverseID;
	Packager.AddToPackage(&ArtNetDMXPacket);

	// Sending
	FJsonObject PacketSettings;
	PacketSettings.SetNumberField(TEXT("UniverseID"), UniverseID);

	const TArray<uint8>& Buffer = Packager.GetBuffer();
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(PacketSettings, UniverseID, Buffer);
	if (!GetSenderInterface())
	{
		return EDMXSendResult::ErrorEnqueuePackage;
	}

	if (!GetSenderInterface()->EnqueueOutboundPackage(Packet))
	{
		return EDMXSendResult::ErrorEnqueuePackage;
	}

	// Stats
	SCOPE_CYCLE_COUNTER(STAT_ArtNetPackagesEnqueueToSend);

	return EDMXSendResult::Success;
}

void FDMXProtocolArtNet::SendRDMCommand(const TSharedPtr<FJsonObject>& CMD)
{
	// Cross protocol RDM send command implementation will be here
}

void FDMXProtocolArtNet::RDMDiscovery(const TSharedPtr<FJsonObject>& CMD)
{
	// Cross protocol RDM discovery implementation will be here
}

bool FDMXProtocolArtNet::TransmitPoll()
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// Poll packet
	FDMXProtocolArtNetPollPacket PollPacket;
	Packager.AddToPackage(&PollPacket);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	return GetSenderInterface()->EnqueueOutboundPackage(Packet);
}

bool FDMXProtocolArtNet::TransmitTodRequestToAll()
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitTodRequest packet
	FDMXProtocolArtNetTodRequest TodRequest;
	for (TMap<uint32, TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe>>::TConstIterator UniverseIt = UniverseManager->GetAllUniverses().CreateConstIterator(); UniverseIt; ++UniverseIt)
	{
		if (UniverseIt->Value.IsValid())
		{
			WriteTodRequestAddress(TodRequest, UniverseIt->Value->GetPortAddress());
		}
	}

	Packager.AddToPackage(&TodRequest);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	return GetSenderInterface()->EnqueueOutboundPackage(Packet);
}

bool FDMXProtocolArtNet::TransmitTodRequest(uint8 PortAddress)
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitTodRequest packet
	FDMXProtocolArtNetTodRequest TodRequest;
	WriteTodRequestAddress(TodRequest, PortAddress);

	Packager.AddToPackage(&TodRequest);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	return GetSenderInterface()->EnqueueOutboundPackage(Packet);
}

void FDMXProtocolArtNet::WriteTodRequestAddress(FDMXProtocolArtNetTodRequest& TodRequest, uint8 PortAddress)
{
	TodRequest.Address[TodRequest.AdCount] = PortAddress;
	TodRequest.AdCount++;
}

bool FDMXProtocolArtNet::TransmitTodData(uint32 InUniverseID)
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitTodData packet
	FDMXProtocolArtNetTodData TodData;
	TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(InUniverseID);

	if (Universe.IsValid())
	{
		TodData.Port = Universe->GetPortID();
		TodData.Net = Universe->GetNetAddress();
		TodData.Address = Universe->GetUniverseAddress();

		const TArray<FRDMUID>& TODUID = Universe->GetTODUIDs();
		const uint32 NumTODBytes = TODUID.Num() * ARTNET_RDM_UID_WIDTH;

		TodData.UidTotalHi = DMX_SHORT_GET_HIGH_BIT(TODUID.Num());
		TodData.UidTotal = DMX_SHORT_GET_LOW_BYTE(TODUID.Num());

		if (TODUID.Num())
		{
			TodData.BlockCount = 1;
			TodData.UidCount = TODUID.Num();
			FMemory::Memcpy(TodData.Tod, TODUID.GetData(), NumTODBytes);
		}
	}

	Packager.AddToPackage(&TodData);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	return GetSenderInterface()->EnqueueOutboundPackage(Packet);
}

bool FDMXProtocolArtNet::TransmitTodControl(uint32 InUniverseID, uint8 Action)
{
	// Try to get Universe
	TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(InUniverseID);

	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitTodControl packet
	FDMXProtocolArtNetTodControl TodControl;
	TodControl.Cmd = Action;

	if (Universe.IsValid())
	{
		TodControl.Net = Universe->GetNetAddress();
		TodControl.Address = Universe->GetPortAddress();
	}

	Packager.AddToPackage(&TodControl);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	return GetSenderInterface()->EnqueueOutboundPackage(Packet);
}

bool FDMXProtocolArtNet::TransmitRDM(uint32 InUniverseID, const TArray<uint8>& Data)
{
	// Try to get Universe
	TSharedPtr<FDMXProtocolUniverseArtNet, ESPMode::ThreadSafe> Universe = UniverseManager->GetUniverseById(InUniverseID);

	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitRDM packet
	FDMXProtocolArtNetRDM RDM;
	if (Universe.IsValid())
	{
		RDM.Net = Universe->GetNetAddress();
		RDM.Address = Universe->GetUniverseAddress();
	}

	if (Data.Num() <= ARTNET_MAX_RDM_DATA)
	{
		FMemory::Memcpy(RDM.Data, Data.GetData(), ARTNET_MAX_RDM_DATA);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("%s:Size of outcoming ArtNet RDM commands is bigger then limit"), NetworkErrorMessagePrefix);
	}

	Packager.AddToPackage(&RDM);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	return GetSenderInterface()->EnqueueOutboundPackage(Packet);
}

void FDMXProtocolArtNet::OnDataReceived(const FArrayReaderPtr& Buffer)
{
	// It will be more handlers
	switch (ArtNet::GetPacketType(Buffer))
	{
	case ARTNET_POLL:
		HandlePool(Buffer);
		break;
	case ARTNET_REPLY:
		HandleReplyPacket(Buffer);
		break;
	case ARTNET_DMX:
		HandleDataPacket(Buffer);
		break;
	case ARTNET_TODREQUEST:
		HandleTodRequest(Buffer);
		break;
	case ARTNET_TODDATA:
		HandleTodData(Buffer);
		break;
	case ARTNET_TODCONTROL:
		HandleTodControl(Buffer);
		break;
	case ARTNET_RDM:
		HandleRdm(Buffer);
	default:
		break;
	}
}

bool FDMXProtocolArtNet::HandlePool(const FArrayReaderPtr& Buffer)
{
	*Buffer << IncomingPollPacket;

	// Nothing to do with packet now
	return true;
}

bool FDMXProtocolArtNet::HandleReplyPacket(const FArrayReaderPtr& Buffer)
{
	*Buffer << PacketReply;

	// Nothing to do with packet now
	return false;
}

bool FDMXProtocolArtNet::HandleDataPacket(const FArrayReaderPtr& Buffer)
{
	uint16 UniverseId = 0x0000;
	const uint32 MinCheck = ARTNET_UNIVERSE_ADDRESS + 2;
	if (Buffer->Num() > MinCheck)
	{
		// Get OpCode
		Buffer->Seek(ARTNET_UNIVERSE_ADDRESS);
		*Buffer << UniverseId;

		// Reset Position
		Buffer->Seek(0);

		FDMXProtocolArtNetDMXPacket ArtNetDMXPacket;
		*Buffer << ArtNetDMXPacket;

		if (ReceivingRunnable.IsValid())
		{
			ReceivingRunnable->PushDMXPacket(UniverseId, ArtNetDMXPacket);
		}
	}

	return true;
}

bool FDMXProtocolArtNet::HandleTodRequest(const FArrayReaderPtr& Buffer)
{
	*Buffer << IncomingTodRequest;

	return true;
}

bool FDMXProtocolArtNet::HandleTodData(const FArrayReaderPtr& Buffer)
{
	*Buffer << IncomingTodData;

	return true;
}

bool FDMXProtocolArtNet::HandleTodControl(const FArrayReaderPtr& Buffer)
{
	*Buffer << IncomingTodControl;

	return true;
}

bool FDMXProtocolArtNet::HandleRdm(const FArrayReaderPtr& Buffer)
{
	*Buffer << IncomingRDM;

	return true;
}

uint32 FDMXProtocolArtNet::GetUniverseAddr(FString UnicastAddress) const
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedPtr<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();
	uint32 ReturnAddress = 0;
	if (UnicastAddress.IsEmpty())
	{
		GetBroadcastAddr()->GetIp(ReturnAddress);
		return ReturnAddress;
	}
	else
	{
		bool bIsValid = false;
		InternetAddr->SetIp(*UnicastAddress, bIsValid);
		InternetAddr->GetIp(ReturnAddress);
		return ReturnAddress;
	}

	return ReturnAddress;
}

void FDMXProtocolArtNet::ReleaseArtNetReceiver()
{
	ReceivingRunnable.Reset();
	ReceivingRunnable = nullptr;
}
