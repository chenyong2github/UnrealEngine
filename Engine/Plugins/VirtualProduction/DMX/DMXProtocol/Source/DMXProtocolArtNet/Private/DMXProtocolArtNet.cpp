// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolArtNet.h"

#include "DMXProtocolArtNetConstants.h"
#include "DMXProtocolArtNetReceiver.h"
#include "DMXProtocolArtNetSender.h"
#include "Interfaces/IDMXSender.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"


const TArray<EDMXCommunicationType> FDMXProtocolArtNet::InputPortCommunicationTypes = TArray<EDMXCommunicationType>(
	{ 
		EDMXCommunicationType::InternalOnly 
	});

const TArray<EDMXCommunicationType> FDMXProtocolArtNet::OutputPortCommunicationTypes = TArray<EDMXCommunicationType>(
	{ 
		EDMXCommunicationType::Broadcast, 
		EDMXCommunicationType::Unicast 
	});

FDMXProtocolArtNet::FDMXProtocolArtNet(const FName& InProtocolName)
	: ProtocolName(InProtocolName)	
{}

bool FDMXProtocolArtNet::Init()
{
	return true;
}

bool FDMXProtocolArtNet::Shutdown()
{
	return true;
}

bool FDMXProtocolArtNet::Tick(float DeltaTime)
{
	checkNoEntry();
	return true;
}

bool FDMXProtocolArtNet::IsEnabled() const
{
	return true;
}

const FName& FDMXProtocolArtNet::GetProtocolName() const
{
	return ProtocolName;
}

int32 FDMXProtocolArtNet::GetMinUniverseID() const
{
	return 0;
}

int32 FDMXProtocolArtNet::GetMaxUniverseID() const
{
	return ARTNET_MAX_UNIVERSE;
}

bool FDMXProtocolArtNet::IsValidUniverseID(int32 UniverseID) const
{
	return
		UniverseID >= ARTNET_MIN_UNIVERSE &&
		UniverseID <= ARTNET_MAX_UNIVERSE;
}

int32 FDMXProtocolArtNet::MakeValidUniverseID(int32 DesiredUniverseID) const
{
	return FMath::Clamp(DesiredUniverseID, static_cast<int32>(ARTNET_MIN_UNIVERSE), static_cast<int32>(ARTNET_MAX_UNIVERSE));
}

const TArray<EDMXCommunicationType> FDMXProtocolArtNet::GetInputPortCommunicationTypes() const
{
	return InputPortCommunicationTypes;
}

const TArray<EDMXCommunicationType> FDMXProtocolArtNet::GetOutputPortCommunicationTypes() const
{
	return OutputPortCommunicationTypes;
}

bool FDMXProtocolArtNet::RegisterInputPort(const FDMXInputPortSharedRef& InputPort)
{
	check(!InputPort->IsRegistered());
	check(!CachedInputPorts.Contains(InputPort));

	const FString& IPAddress = InputPort->GetAddress();
	const EDMXCommunicationType CommunicationType = InputPort->GetCommunicationType();

	// Try to use an existing receiver or create a new one
	TSharedPtr<FDMXProtocolArtNetReceiver> Receiver = FindExistingEndpointReceiver(IPAddress, CommunicationType);
	if (!Receiver.IsValid())
	{
		Receiver = FDMXProtocolArtNetReceiver::TryCreate(SharedThis(this), IPAddress);
	}

	if (!Receiver.IsValid())
	{
		UE_LOG(LogDMXProtocol, Warning, TEXT("Could not create Art-Net receiver for input port %s"), *InputPort->GetPortName());
		
		return false;
	}

	Receivers.Add(Receiver);

	Receiver->AssignInputPort(InputPort);
	CachedInputPorts.Add(InputPort);
	
	return true;
}

void FDMXProtocolArtNet::UnregisterInputPort(const FDMXInputPortSharedRef& InputPort)
{
	check(CachedInputPorts.Contains(InputPort));
	CachedInputPorts.Remove(InputPort);

	TSharedPtr<FDMXProtocolArtNetReceiver> UnusedReceiver;
	for (const TSharedPtr<FDMXProtocolArtNetReceiver>& Receiver : Receivers)
	{
		if (Receiver->ContainsInputPort(InputPort))
		{
			Receiver->UnassignInputPort(InputPort);

			if (Receiver->GetNumAssignedInputPorts() == 0)
			{
				UnusedReceiver = Receiver;
			}
			break;
		}
	}

	if (UnusedReceiver.IsValid())
	{
		Receivers.Remove(UnusedReceiver);
	}
}

TSharedPtr<IDMXSender> FDMXProtocolArtNet::RegisterOutputPort(const FDMXOutputPortSharedRef& OutputPort)
{
	check(!OutputPort->IsRegistered());
	check(!CachedOutputPorts.Contains(OutputPort));

	const FString& IPAddress = OutputPort->GetAddress();
	EDMXCommunicationType CommunicationType = OutputPort->GetCommunicationType();

	// Try to use an existing receiver or create a new one
	TSharedPtr<FDMXProtocolArtNetSender> Sender = FindExistingEndpointSender(IPAddress, CommunicationType);
	if (!Sender.IsValid())
	{
		Sender = FDMXProtocolArtNetSender::TryCreate(SharedThis(this), IPAddress, CommunicationType);
	}

	if (!Sender.IsValid())
	{
		UE_LOG(LogDMXProtocol, Warning, TEXT("Could not create Art-Net sender for output port %s"), *OutputPort->GetPortName());

		return nullptr;
	}

	Senders.Add(Sender);

	Sender->AssignOutputPort(OutputPort);
	CachedOutputPorts.Add(OutputPort);

	return Sender;
}

void FDMXProtocolArtNet::UnregisterOutputPort(const FDMXOutputPortSharedRef& OutputPort)
{
	check(CachedOutputPorts.Contains(OutputPort));
	CachedOutputPorts.Remove(OutputPort);

	TSharedPtr<FDMXProtocolArtNetSender> UnusedSender;
	for (const TSharedPtr<FDMXProtocolArtNetSender>& Sender : Senders)
	{
		if (Sender->ContainsOutputPort(OutputPort))
		{
			Sender->UnassignOutputPort(OutputPort);

			if (Sender->GetNumAssignedOutputPorts() == 0)
			{
				UnusedSender = Sender;
			}
			break;
		}
	}

	if (UnusedSender.IsValid())
	{
		Senders.Remove(UnusedSender);
	}
}

bool FDMXProtocolArtNet::IsCausingLoopback(EDMXCommunicationType InCommunicationType)
{
	return InCommunicationType == EDMXCommunicationType::Broadcast;
}

TSharedPtr<FDMXProtocolArtNetSender> FDMXProtocolArtNet::FindExistingEndpointSender(const FString& IPAddress, EDMXCommunicationType CommunicationType) const
{
	for (const TSharedPtr<FDMXProtocolArtNetSender>& Sender : Senders)
	{
		if (Sender->EqualsEndpoint(IPAddress, CommunicationType))
		{
			return Sender;
		}
	}

	return nullptr;
}

TSharedPtr<FDMXProtocolArtNetReceiver> FDMXProtocolArtNet::FindExistingEndpointReceiver(const FString& IPAddress, EDMXCommunicationType CommunicationType) const
{
	for (const TSharedPtr<FDMXProtocolArtNetReceiver>& Receiver : Receivers)
	{
		if (Receiver->EqualsEndpoint(IPAddress))
		{
			return Receiver;
		}
	}

	return nullptr;
}
