// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Devices/VRPN/Tracker/DisplayClusterVrpnTrackerInputDataHolder.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterTypesConverter.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterVrpnTrackerInputDataHolder::FDisplayClusterVrpnTrackerInputDataHolder(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceTracker* CfgDevice)
	: FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnTracker>(DeviceId, CfgDevice)
{
}

FDisplayClusterVrpnTrackerInputDataHolder::~FDisplayClusterVrpnTrackerInputDataHolder()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterVrpnTrackerInputDataHolder::Initialize()
{
	return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterStringSerializable
//////////////////////////////////////////////////////////////////////////////////////////////
FString FDisplayClusterVrpnTrackerInputDataHolder::SerializeToString() const
{
	FString Result;
	Result.Reserve(256);

	for (auto it = DeviceData.CreateConstIterator(); it; ++it)
	{
		Result += FString::Printf(TEXT("%d%s%s%s%s%s"),
			it->Key,
			SerializationDelimiter,
			*DisplayClusterTypesConverter::template ToHexString(it->Value.TrackerLoc),
			SerializationDelimiter,
			*DisplayClusterTypesConverter::template ToHexString(it->Value.TrackerQuat),
			SerializationDelimiter);
	}

	return Result;
}

bool FDisplayClusterVrpnTrackerInputDataHolder::DeserializeFromString(const FString& Data)
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
		const int  ch = FCString::Atoi(*Parsed[i]);
		const FVector  Loc  = DisplayClusterTypesConverter::template FromHexString<FVector>(Parsed[i + 1]);
		const FQuat    Quat = DisplayClusterTypesConverter::template FromHexString<FQuat>(Parsed[i + 2]);

		DeviceData.Add(ch, FDisplayClusterVrpnTrackerChannelData{ Loc, Quat });
	}

	return true;
}
