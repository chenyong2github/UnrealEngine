// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterVrpnKeyboardInputDataHolder.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterVrpnKeyboardInputDataHolder::FDisplayClusterVrpnKeyboardInputDataHolder(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceKeyboard* CfgDevice)
	: FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnKeyboard>(DeviceId, CfgDevice)
{
}

FDisplayClusterVrpnKeyboardInputDataHolder::~FDisplayClusterVrpnKeyboardInputDataHolder()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterVrpnKeyboardInputDataHolder::Initialize()
{
	return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterStringSerializable
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterVrpnKeyboardInputDataHolder::SerializeToString() const
{
	FString Result;
	Result.Reserve(64);

	for (auto it = DeviceData.CreateConstIterator(); it; ++it)
	{
		Result += FString::Printf(TEXT("%d%s%d%s%d%s"), it->Key, SerializationDelimiter, it->Value.BtnStateOld, SerializationDelimiter, it->Value.BtnStateNew, SerializationDelimiter);
	}

	return Result;
}

bool FDisplayClusterVrpnKeyboardInputDataHolder::DeserializeFromString(const FString& Data)
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
		const bool GetStateOld = (FCString::Atoi(*Parsed[i + 1]) != 0);
		const bool GetStateNew = (FCString::Atoi(*Parsed[i + 2]) != 0);
		DeviceData.Add(Ch, FDisplayClusterVrpnKeyboardChannelData{ GetStateOld, GetStateNew });
	}

	return true;
}
