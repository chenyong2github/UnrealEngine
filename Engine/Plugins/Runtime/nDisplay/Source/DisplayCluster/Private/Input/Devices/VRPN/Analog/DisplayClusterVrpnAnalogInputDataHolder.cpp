// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Devices/VRPN/Analog/DisplayClusterVrpnAnalogInputDataHolder.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterTypesConverter.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterVrpnAnalogInputDataHolder::FDisplayClusterVrpnAnalogInputDataHolder(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceAnalog* CfgDevice)
	: FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnAnalog>(DeviceId, CfgDevice)
{
}

FDisplayClusterVrpnAnalogInputDataHolder::~FDisplayClusterVrpnAnalogInputDataHolder()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterVrpnAnalogInputDataHolder::Initialize()
{
	return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterStringSerializable
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterVrpnAnalogInputDataHolder::SerializeToString() const
{
	FString Result;
	Result.Reserve(128);

	for (auto it : DeviceData) 
	{
		Result += FString::Printf(TEXT("%d%s%s%s"), it.Key, SerializationDelimiter, *DisplayClusterTypesConverter::template ToHexString(it.Value.AxisValue), SerializationDelimiter);
	}

	return Result;
}

bool FDisplayClusterVrpnAnalogInputDataHolder::DeserializeFromString(const FString& Data)
{
	TArray<FString> Parsed;
	Data.ParseIntoArray(Parsed, SerializationDelimiter);

	if (Parsed.Num() % SerializationItems)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Error, TEXT("Wrong items amount after deserialization [%s]"), *Data);
		return false;
	}

	for (int i = 0; i < Parsed.Num(); i += SerializationItems)
	{
		const int   Ch  = FCString::Atoi(*Parsed[i]);
		const float Val = DisplayClusterTypesConverter::template FromHexString<float>(*Parsed[i + 1]); // SerializationItems == 2 so [i + 1] is safe
		DeviceData.Add(Ch, FDisplayClusterVrpnAnalogChannelData{ Val });
	}

	return true;
}
