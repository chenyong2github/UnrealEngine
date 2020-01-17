// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolDeviceSACN.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolInterface.h"
#include "Interfaces/IDMXProtocolPort.h"
#include "DMXProtocolTypes.h"
#include "Dom/JsonObject.h"

FDMXProtocolDeviceSACN::FDMXProtocolDeviceSACN(IDMXProtocol* InDMXProtocol, TSharedPtr<IDMXProtocolInterface> InProtocolInterface, FJsonObject& InSettings, uint32 InDeviceID)
	: DMXProtocol(InDMXProtocol)
	, ProtocolInterface(InProtocolInterface)
	, DeviceID(InDeviceID)
{
	Settings = MakeShared<FJsonObject>(InSettings);

	checkf(DMXProtocol, TEXT("DMXProtocol pointer is nullptr"));
	checkf(ProtocolInterface.IsValid(), TEXT("ProtocolInterface is not valid"));
}

TSharedPtr<FJsonObject> FDMXProtocolDeviceSACN::GetSettings() const
{
	return Settings;
}

TWeakPtr<IDMXProtocolInterface> FDMXProtocolDeviceSACN::GetCachedProtocolInterface() const
{
	return ProtocolInterface;
}

IDMXProtocol* FDMXProtocolDeviceSACN::GetProtocol() const
{
	return DMXProtocol;
}

uint32 FDMXProtocolDeviceSACN::GetDeviceID() const
{
	return DeviceID;
}

bool FDMXProtocolDeviceSACN::AllowLooping() const
{
	return true;
}
