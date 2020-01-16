// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolDeviceArtNet.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolInterface.h"
#include "Interfaces/IDMXProtocolPort.h"
#include "Managers/DMXProtocolPortManager.h"
#include "DMXProtocolTypes.h"
#include "Dom/JsonObject.h"

FDMXProtocolDeviceArtNet::FDMXProtocolDeviceArtNet(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolInterface> InProtocolInterface, FJsonObject& InSettings, uint32 InDeviceID)
	: DMXProtocol(InDMXProtocol)
	, ProtocolInterface(InProtocolInterface)
	, DeviceID(InDeviceID)
{
	Settings = MakeShared<FJsonObject>(InSettings);

	checkf(DMXProtocol, TEXT("DMXProtocol pointer is nullptr"));
	checkf(ProtocolInterface.IsValid(), TEXT("ProtocolInterface is not valid"));
}

TSharedPtr<FJsonObject> FDMXProtocolDeviceArtNet::GetSettings() const
{
	return Settings;
}

TWeakPtr<IDMXProtocolInterface> FDMXProtocolDeviceArtNet::GetCachedProtocolInterface() const
{
	return ProtocolInterface;
}

IDMXProtocol* FDMXProtocolDeviceArtNet::GetProtocol() const
{
	return DMXProtocol;
}

uint32 FDMXProtocolDeviceArtNet::GetDeviceID() const
{
	return DeviceID;
}

bool FDMXProtocolDeviceArtNet::AllowLooping() const
{
	return true;
}
