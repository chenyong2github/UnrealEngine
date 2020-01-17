// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolArtNet.h"
#include "DMXProtocolTransportArtNet.h"
#include "Common/UdpSocketBuilder.h"

#include "DMXProtocolSettings.h"
#include "Managers/DMXProtocolUniverseManager.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXProtocolArtNetUtils.h"
#include "DMXProtocolPortArtNet.h"
#include "DMXProtocolPackager.h"
#include "Managers/DMXProtocolDeviceManager.h"
#include "Managers/DMXProtocolPortManager.h"

using namespace ArtNet;

TSharedPtr<IDMXProtocolSender> FDMXProtocolArtNet::GetSenderInterface() const
{
	return ArtNetSender;
}

bool FDMXProtocolArtNet::Init()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);

	// Create broadcast address
	BroadcastAddr = SocketSubsystem->CreateInternetAddr();
	BroadcastAddr->SetBroadcastAddress();
	BroadcastAddr->SetPort(ARTNET_PORT);
	BroadcastEndpoint = FIPv4Endpoint(BroadcastAddr);

	// Create sender address
	SenderAddr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid;
	FString InterfaceIPAddress = GetDefault<UDMXProtocolSettings>()->InterfaceIPAddress;
	SenderAddr->SetIp(*InterfaceIPAddress, bIsValid);
	SenderAddr->SetPort(ARTNET_PORT);
	SenderEndpoint = FIPv4Endpoint(SenderAddr);

	// Create Broadcast socket
	BroadcastSocket = FUdpSocketBuilder(TEXT("UDPArtNetBroadcastSocket"))
		.AsNonBlocking()
		.AsReusable()
		.WithBroadcast()
#if PLATFORM_WINDOWS
		.BoundToAddress(SenderEndpoint.Address)
#endif
		.BoundToPort(SenderEndpoint.Port);

	if (BroadcastSocket == nullptr)
	{
		UE_LOG_DMXPROTOCOL(Error, TEXT("ERROR create BroadcastSocket"));
		return false;
	}

	// Create sender
	ArtNetSender = MakeShared<FDMXProtocolSenderArtNet>(*BroadcastSocket, this);

	// Create receiver
	FTimespan ThreadWaitTime = FTimespan::FromMilliseconds(100);
	ArtNetReceiver = MakeShared<FDMXProtocolReceiverArtNet>(*BroadcastSocket, this, ThreadWaitTime);
	ArtNetReceiver->OnDataReceived().BindRaw(this, &FDMXProtocolArtNet::OnDataReceived);

	return true;
}

bool FDMXProtocolArtNet::Shutdown()
{
	ArtNetSender.Reset();
	ArtNetReceiver.Reset();

	//Clear all sockets!
	if (BroadcastSocket)
	{
		BroadcastSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(BroadcastSocket);
	}

	return false;
}

bool FDMXProtocolArtNet::IsEnabled() const
{
	return true;
}

FName FDMXProtocolArtNet::GetProtocolName() const
{
	return FDMXProtocolArtNetModule::NAME_Artnet;
}

void FDMXProtocolArtNet::Reload()
{
}

bool FDMXProtocolArtNet::Tick(float DeltaTime)
{
	return true;
}

bool FDMXProtocolArtNet::SendDMX(uint16 UniverseID, uint8 PortID, const TSharedPtr<FDMXBuffer>& DMXBuffer) const
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// ArtNet DMX packet
	FDMXProtocolArtNetDMXPacket ArtNetDMXPacket;
	if (DMXBuffer->GetDMXData().Num() == ARTNET_DMX_LENGTH)
	{
		FMemory::Memcpy(ArtNetDMXPacket.Data, DMXBuffer->GetDMXData().GetData(), ARTNET_DMX_LENGTH);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Size of outcoming ArtNet DMX buffer is wrong"));
	}
	ArtNetDMXPacket.Physical = PortID;
	ArtNetDMXPacket.Universe = UniverseID;
	Packager.AddToPackage(&ArtNetDMXPacket);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	GetSenderInterface()->EnqueueOutboundPackage(Packet);

	return false;
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
	bool bIsSent = false;
	IDevicesMap DevicesMap;
	if (GetDeviceManager()->GetDevicesByProtocol(GetProtocolName(), DevicesMap))
	{
		for (TMap<IDMXProtocolInterface*, TSharedPtr<IDMXProtocolDevice>>::TConstIterator DeviceIt = DevicesMap.CreateConstIterator(); DeviceIt; ++DeviceIt)
		{
			if (DeviceIt->Value.IsValid())
			{
				TransmitTodRequest(DeviceIt->Value);
				bIsSent = true;
			}
		}
	}

	return bIsSent;
}

bool FDMXProtocolArtNet::TransmitTodRequest(const TSharedPtr<IDMXProtocolDevice>& InDevice)
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitTodRequest packet
	FDMXProtocolArtNetTodRequest TodRequest;

	if (InDevice.IsValid())
	{
		const IPortsMap* OutputPortsMapDevice = GetPortManager()->GetOutputPortMapByDevice(InDevice.Get());
		for (uint8 PortIndex = 0; PortIndex < ARTNET_MAX_PORTS; PortIndex++)
		{
			if (OutputPortsMapDevice != nullptr)
			{
				const TSharedPtr<IDMXProtocolPort>* Port = OutputPortsMapDevice->Find(PortIndex);
				if (Port != nullptr && Port->IsValid())
				{
					FDMXProtocolPortArtNet* PortArtNet = static_cast<FDMXProtocolPortArtNet*>(Port->Get());
					TodRequest.Address[TodRequest.AdCount] = PortArtNet->GetPortAddress();
					TodRequest.AdCount++;
				}
			}
		}
	}

	Packager.AddToPackage(&TodRequest);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	return GetSenderInterface()->EnqueueOutboundPackage(Packet);
}

bool FDMXProtocolArtNet::TransmitTodData(const TSharedPtr<IDMXProtocolDevice>& InDevice, uint8 InPortID)
{
	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitTodData packet
	FDMXProtocolArtNetTodData TodData;
	TodData.Port = InPortID;

	FDMXProtocolPortArtNet* PortArtNet = static_cast<FDMXProtocolPortArtNet*>(GetPortManager()->GetPortByDeviceAndID(InDevice, InPortID, EDMXPortDirection::DMX_PORT_OUTPUT));
	if (PortArtNet != nullptr)
	{
		TodData.Net = PortArtNet->GetNetAddress();
		TodData.Address = PortArtNet->GetUniverseAddress();

		const TArray<FRDMUID>& TODUID = PortArtNet->GetTODUIDs();
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

bool FDMXProtocolArtNet::TransmitTodControl(const TSharedPtr<IDMXProtocolDevice>& InDevice, uint8 InPortID, uint8 Action)
{
	// Try to get ArtNet Port
	FDMXProtocolPortArtNet* PortArtNet = static_cast<FDMXProtocolPortArtNet*>(GetPortManager()->GetPortByDeviceAndID(InDevice, InPortID, EDMXPortDirection::DMX_PORT_OUTPUT));

	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitTodControl packet
	FDMXProtocolArtNetTodControl TodControl;
	TodControl.Cmd = Action;

	if (PortArtNet != nullptr)
	{
		TodControl.Net = PortArtNet->GetNetAddress();
		TodControl.Address = PortArtNet->GetPortAddress();
	}

	Packager.AddToPackage(&TodControl);

	// Sending
	FDMXPacketPtr Packet = MakeShared<FDMXPacket, ESPMode::ThreadSafe>(Packager.GetBuffer());
	return GetSenderInterface()->EnqueueOutboundPackage(Packet);
}

bool FDMXProtocolArtNet::TransmitRDM(const TSharedPtr<IDMXProtocolDevice>& InDevice, uint8 InPortID, const TArray<uint8>& Data)
{
	// Try to get ArtNet Port
	FDMXProtocolPortArtNet* PortArtNet = static_cast<FDMXProtocolPortArtNet*>(GetPortManager()->GetPortByDeviceAndID(InDevice, InPortID, EDMXPortDirection::DMX_PORT_OUTPUT));

	// Init Packager
	FDMXProtocolPackager Packager;

	// TransmitRDM packet
	FDMXProtocolArtNetRDM RDM;
	if (PortArtNet != nullptr)
	{
		RDM.Net = PortArtNet->GetNetAddress();
		RDM.Address = PortArtNet->GetUniverseAddress();
	}

	if (Data.Num() <= ARTNET_MAX_RDM_DATA)
	{
		FMemory::Memcpy(RDM.Data, Data.GetData(), ARTNET_MAX_RDM_DATA);
	}
	else
	{
		UE_LOG_DMXPROTOCOL(Warning, TEXT("Size of outcoming ArtNet RDM commands is bigger then limit"));
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

bool FDMXProtocolArtNet::HandleReplyPacket(const FArrayReaderPtr & Buffer)
{
	FArtNetPacketReply ArtNetPacketReply;

	*Buffer << ArtNetPacketReply;

	// Nothing to do with packet now
	return false;
}

bool FDMXProtocolArtNet::HandleDataPacket(const FArrayReaderPtr & Buffer)
{
	// ArtNet DMX packet
	FDMXProtocolArtNetDMXPacket ArtNetDMXPacket;
	*Buffer << ArtNetDMXPacket;

	// Write data to input DMX buffer universe
	IDMXProtocolUniverse* Universe = GetUniverseManager()->GetUniverseById(ArtNetDMXPacket.Universe);
	if (Universe != nullptr && Universe->GetInputDMXBuffer().IsValid())
	{
		// Make sure we copy same amount of data
		if (Universe->GetInputDMXBuffer()->GetDMXData().Num() == ARTNET_DMX_LENGTH)
		{
			FMemory::Memcpy(Universe->GetInputDMXBuffer()->GetDMXData().GetData(), ArtNetDMXPacket.Data, ARTNET_DMX_LENGTH);
			return true;
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Warning, TEXT("Size of incoming DMX buffer is wrong"));
			return false;
		}
	}

	return false;
}

bool FDMXProtocolArtNet::HandleTodRequest(const FArrayReaderPtr & Buffer)
{
	*Buffer << IncomingTodRequest;

	return true;
}

bool FDMXProtocolArtNet::HandleTodData(const FArrayReaderPtr & Buffer)
{
	*Buffer << IncomingTodData;

	return true;
}

bool FDMXProtocolArtNet::HandleTodControl(const FArrayReaderPtr & Buffer)
{
	*Buffer << IncomingTodControl;

	return true;
}

bool FDMXProtocolArtNet::HandleRdm(const FArrayReaderPtr & Buffer)
{
	*Buffer << IncomingRDM;

	return true;
}


