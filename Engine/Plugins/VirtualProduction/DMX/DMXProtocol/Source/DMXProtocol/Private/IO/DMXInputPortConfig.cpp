// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/DMXInputPortConfig.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolUtils.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXPortManager.h"

#include "Misc/Guid.h"


FDMXInputPortConfigParams::FDMXInputPortConfigParams(const FDMXInputPortConfig& InputPortConfig)
	: PortName(InputPortConfig.GetPortName())
	, ProtocolName(InputPortConfig.GetProtocolName())
	, CommunicationType(InputPortConfig.GetCommunicationType())
	, DeviceAddress(InputPortConfig.GetDeviceAddress())
	, LocalUniverseStart(InputPortConfig.GetLocalUniverseStart())
	, NumUniverses(InputPortConfig.GetNumUniverses())
	, ExternUniverseStart(InputPortConfig.GetExternUniverseStart())
{}

FDMXInputPortConfig::FDMXInputPortConfig(const FGuid& InPortGuid)
	: PortGuid(InPortGuid)
{
	// Cannot create port configs before the protocol module is up (it is required to sanetize protocol names).
	check(FModuleManager::Get().IsModuleLoaded("DMXProtocol"));
	check(PortGuid.IsValid());

	GenerateUniquePortName();

	MakeValid();
}

FDMXInputPortConfig::FDMXInputPortConfig(const FGuid& InPortGuid, const FDMXInputPortConfigParams& InitializationData)	
	: PortName(InitializationData.PortName)
	, ProtocolName(InitializationData.ProtocolName)
	, CommunicationType(InitializationData.CommunicationType)
	, DeviceAddress(InitializationData.DeviceAddress)
	, LocalUniverseStart(InitializationData.LocalUniverseStart)
	, NumUniverses(InitializationData.NumUniverses)
	, ExternUniverseStart(InitializationData.ExternUniverseStart)
	, PortGuid(InPortGuid)
{
	// Cannot create port configs before the protocol module is up (it is required to sanetize protocol names).
	check(FModuleManager::Get().IsModuleLoaded("DMXProtocol"));
	check(PortGuid.IsValid());
	check(!ProtocolName.IsNone())

	GenerateUniquePortName();

	MakeValid();
}

void FDMXInputPortConfig::MakeValid()
{
	checkf(PortGuid.IsValid(), TEXT("The port guid has to be valid to make a valid config. Use related constructor."))

	// Test the protocol name and make it valid if needed
	IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName);
	if (!Protocol.IsValid())
	{
		const TArray<FName> ProtocolNames = IDMXProtocol::GetProtocolNames();
		if (ProtocolNames.Num() > 0)
		{
			ProtocolName = ProtocolNames[0];
			Protocol = IDMXProtocol::Get(ProtocolName);
		}
	}
	checkf(Protocol.IsValid(), TEXT("Protocols not loaded or no protocols available. Use of port configs is only supported after the engine is fully loaded (PostEngineInit)."));

	// If the extern universe ID is out of the protocol's supported range, mend it.
	ExternUniverseStart = Protocol->MakeValidUniverseID(ExternUniverseStart);

	// Only Local universes > 1 are supported, even if the protocol supports universes < 1.
	LocalUniverseStart = LocalUniverseStart < 1 ? 1 : LocalUniverseStart;

	// Limit the num universes relatively to the max extern universe of the protocol and int32 max
	const int64 MaxNumUniverses = Protocol->GetMaxUniverseID() - Protocol->GetMinUniverseID() + 1;
	const int64 DesiredNumUniverses = NumUniverses > MaxNumUniverses ? MaxNumUniverses : NumUniverses;
	const int64 DesiredLocalUniverseEnd = static_cast<int64>(LocalUniverseStart) + DesiredNumUniverses - 1;

	if (DesiredLocalUniverseEnd > TNumericLimits<int32>::Max())
	{
		NumUniverses = TNumericLimits<int32>::Max() - DesiredLocalUniverseEnd;
	}

	// Fix the communication type if it is not supported by the protocol
	TArray<EDMXCommunicationType> CommunicationTypes = Protocol->GetInputPortCommunicationTypes();
	if (!CommunicationTypes.Contains(CommunicationType))
	{
		if (CommunicationTypes.Num() > 0)
		{
			CommunicationType = CommunicationTypes[0];
		}
		else
		{
			// The protocol can specify none to suggest internal only
			CommunicationType = EDMXCommunicationType::InternalOnly;
		}
	}	
}

void FDMXInputPortConfig::GenerateUniquePortName()
{
	if (!PortName.IsEmpty())
	{
		return;
	}

	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	check(ProtocolSettings);

	TSet<FString> OtherPortNames;
	for (const FDMXInputPortConfig& InputPortConfig : ProtocolSettings->InputPortConfigs)
	{
		if (&InputPortConfig == this)
		{
			continue;
		}

		OtherPortNames.Add(InputPortConfig.PortName);
	}

	FString BaseName = TEXT("InputPort_1");

	PortName = FDMXProtocolUtils::GenerateUniqueNameFromExisting(OtherPortNames, BaseName);
}
