// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Devices/VRPN/Analog/DisplayClusterVrpnAnalogInputDataHolder.h"

#include "Misc/DisplayClusterCommonTypesConverter.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterVrpnAnalogInputDataHolder::FDisplayClusterVrpnAnalogInputDataHolder(const FDisplayClusterConfigInput& config) :
	FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnAnalog>(config)
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

	for (auto it = DeviceData.CreateConstIterator(); it; ++it)
	{
		Result += FString::Printf(TEXT("%d%s%s%s"), it->Key, SerializationDelimiter, *FDisplayClusterTypesConverter::template ToHexString(it->Value.AxisValue), SerializationDelimiter);
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
		const float Val = FDisplayClusterTypesConverter::template FromHexString<float>(*Parsed[i + 1]);
		DeviceData.Add(Ch, FDisplayClusterVrpnAnalogChannelData{ Val });
	}

	return true;
}
