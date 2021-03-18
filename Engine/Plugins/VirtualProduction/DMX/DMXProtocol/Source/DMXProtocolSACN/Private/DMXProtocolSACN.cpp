// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolSACN.h"

#include "DMXProtocolBlueprintLibrary.h"
#include "DMXProtocolSettings.h"
#include "DMXStats.h"
#include "IO/DMXInputPort.h"
#include "Packets/DMXProtocolE131PDUPacket.h"

#include "Common/UdpSocketBuilder.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Serialization/BufferArchive.h"


// Stats
DECLARE_CYCLE_STAT(TEXT("SACN Packages Enqueue To Send"), STAT_SACNPackagesEnqueueToSend, STATGROUP_DMX);

const TArray<EDMXCommunicationType> FDMXProtocolSACN::InputPortCommunicationTypes = TArray<EDMXCommunicationType>(
	{ 
		EDMXCommunicationType::InternalOnly 
	});

const TArray<EDMXCommunicationType> FDMXProtocolSACN::OutputPortCommunicationTypes = TArray<EDMXCommunicationType>(
	{ 
		EDMXCommunicationType::Broadcast, 
		EDMXCommunicationType::Unicast 
	});

FDMXProtocolSACN::FDMXProtocolSACN(const FName& InProtocolName)
	: ProtocolName(InProtocolName)
{}

bool FDMXProtocolSACN::Init()
{
	return true;
}

bool FDMXProtocolSACN::Shutdown()
{
	return true;
}

bool FDMXProtocolSACN::Tick(float DeltaTime)
{
	return true;
}

bool FDMXProtocolSACN::IsEnabled() const
{
	return true;
}

const FName& FDMXProtocolSACN::GetProtocolName() const
{
	return ProtocolName;
}

const TArray<EDMXCommunicationType> FDMXProtocolSACN::GetInputPortCommunicationTypes() const
{
	return InputPortCommunicationTypes;
}

const TArray<EDMXCommunicationType> FDMXProtocolSACN::GetOutputPortCommunicationTypes() const
{
	return OutputPortCommunicationTypes;
}

int32 FDMXProtocolSACN::GetMinUniverseID() const
{
	return ACN_MIN_UNIVERSE;
}

int32 FDMXProtocolSACN::GetMaxUniverseID() const
{
	return ACN_MAX_UNIVERSE;
}

bool FDMXProtocolSACN::IsValidUniverseID(int32 UniverseID) const
{
	return
		UniverseID >= GetMinUniverseID() &&
		UniverseID <= GetMaxUniverseID();
}

int32 FDMXProtocolSACN::MakeValidUniverseID(int32 DesiredUniverseID) const
{
	return FMath::Clamp(DesiredUniverseID, static_cast<int32>(ACN_MIN_UNIVERSE), static_cast<int32>(ACN_MAX_UNIVERSE));
}

bool FDMXProtocolSACN::RegisterInputPort(const TSharedRef<FDMXInputPort, ESPMode::ThreadSafe>& InputPort)
{
	check(!InputPort->IsRegistered());

	check(!CachedInputPorts.Contains(InputPort));
	CachedInputPorts.Add(InputPort);

	return false;
}

void FDMXProtocolSACN::UnregisterInputPort(const TSharedRef<FDMXInputPort, ESPMode::ThreadSafe>& InputPort)
{
	check(CachedInputPorts.Contains(InputPort));
	CachedInputPorts.Remove(InputPort);
}

TSharedPtr<IDMXSender> FDMXProtocolSACN::RegisterOutputPort(const TSharedRef<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort)
{
	return nullptr;
}

bool FDMXProtocolSACN::IsCausingLoopback(EDMXCommunicationType InCommunicationType)
{
	return false;
}

void FDMXProtocolSACN::UnregisterOutputPort(const TSharedRef<FDMXOutputPort, ESPMode::ThreadSafe>& OutputPort)
{

}
