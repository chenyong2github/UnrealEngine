// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Devices/VRPN/Button/DisplayClusterVrpnButtonInputDataHolder.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterVrpnButtonInputDataHolder::FDisplayClusterVrpnButtonInputDataHolder(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceButton* CfgDevice)
	: FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnButton>(DeviceId, CfgDevice)
{
}

FDisplayClusterVrpnButtonInputDataHolder::~FDisplayClusterVrpnButtonInputDataHolder()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterVrpnButtonInputDataHolder::Initialize()
{
	return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterStringSerializable
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterVrpnButtonInputDataHolder::SerializeToString() const
{
	FString Result;
	Result.Reserve(64);

	for (auto it = DeviceData.CreateConstIterator(); it; ++it)
	{
		Result += FString::Printf(TEXT("%d%s%d%s%d%s"), it->Key, SerializationDelimiter, it->Value.BtnStateOld, SerializationDelimiter, it->Value.BtnStateNew, SerializationDelimiter);
	}

	return Result;
}

bool FDisplayClusterVrpnButtonInputDataHolder::DeserializeFromString(const FString& Data)
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
		const int  Ch = FCString::Atoi(*Parsed[i]);
		const bool StateOld = (FCString::Atoi(*Parsed[i + 1]) != 0);
		const bool StateNew = (FCString::Atoi(*Parsed[i + 2]) != 0);
		DeviceData.Add(Ch, FDisplayClusterVrpnButtonChannelData{ StateOld, StateNew });
	}

	return true;
}
