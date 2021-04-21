// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXOutputPortConfig.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolUtils.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXPortManager.h"

#include "Misc/Guid.h"


FDMXOutputPortConfig::FDMXOutputPortConfig(const FGuid& InPortGuid)
	: CommunicationType(EDMXCommunicationType::InternalOnly)
	, DeviceAddress()
	, DestinationAddress()
	, bLoopbackToEngine(true)
	, LocalUniverseStart(1)
	, NumUniverses(10)
	, ExternUniverseStart(1)
	, Priority(100)
	, PortGuid(InPortGuid)
{
	// May be called before the protocol module is loaded, at this point we only expect already sanetized structs
	if (FModuleManager::Get().IsModuleLoaded("DMXProtocol"))
	{
		SanetizePortName();
		SanetizeProtocolName();
		SanetizeCommunicationType();

		IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName);
		check(Protocol.IsValid());
	}
}

const FGuid& FDMXOutputPortConfig::GetPortGuid() const
{	
	return PortGuid;
}

void FDMXOutputPortConfig::SanetizePortName()
{
	if (!PortName.IsEmpty())
	{
		return;
	}

	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	check(ProtocolSettings);

	TSet<FString> OtherPortNames;
	for (const FDMXOutputPortConfig& PortConfig : ProtocolSettings->OutputPortConfigs)
	{
		if (&PortConfig == this)
		{
			continue;
		}

		OtherPortNames.Add(PortConfig.PortName);
	}

	FString BaseName = TEXT("OutputPort_1");
	PortName = FDMXProtocolUtils::GenerateUniqueNameFromExisting(OtherPortNames, BaseName);
}

void FDMXOutputPortConfig::SanetizeProtocolName()
{
	FDMXProtocolModule& DMXProtocolModule = FModuleManager::GetModuleChecked<FDMXProtocolModule>("DMXProtocol");
	const TMap<FName, IDMXProtocolFactory*>& Protocols = DMXProtocolModule.GetProtocolFactories();

	TArray<FName> AvailableProtocolNames;
	Protocols.GenerateKeyArray(AvailableProtocolNames);

	// Sanetize if the protocol name isn't valid
	if (!AvailableProtocolNames.Contains(ProtocolName))
	{
		// At least one protocol is to be expected
		check(AvailableProtocolNames.Num() > 0)

		ProtocolName = AvailableProtocolNames[0];
	}
}

void FDMXOutputPortConfig::SanetizeCommunicationType()
{
	IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName);

	if (Protocol.IsValid())
	{
		const TArray<EDMXCommunicationType> SupportedCommunicationTypes = Protocol->GetOutputPortCommunicationTypes();
		if (!SupportedCommunicationTypes.Contains(CommunicationType) &&
			SupportedCommunicationTypes.Num() > 0)
		{
			CommunicationType = SupportedCommunicationTypes[0];

			bLoopbackToEngine = Protocol->IsCausingLoopback(CommunicationType);
		}
	}
	else
	{
		CommunicationType = EDMXCommunicationType::InternalOnly;
	}
}