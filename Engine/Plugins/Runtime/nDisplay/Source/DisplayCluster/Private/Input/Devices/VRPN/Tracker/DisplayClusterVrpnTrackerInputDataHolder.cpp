// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Devices/VRPN/Tracker/DisplayClusterVrpnTrackerInputDataHolder.h"
#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"
#include "DisplayClusterLog.h"


FDisplayClusterVrpnTrackerInputDataHolder::FDisplayClusterVrpnTrackerInputDataHolder(const FDisplayClusterConfigInput& config) :
	FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnTracker>(config)
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
	FString result;
	result.Reserve(256);

	for (auto it = DeviceData.CreateConstIterator(); it; ++it)
	{
		result += FString::Printf(TEXT("%d%s%s%s%s%s"),
			it->Key,
			SerializationDelimiter,
			*FDisplayClusterTypesConverter::template ToHexString(it->Value.trLoc),
			SerializationDelimiter,
			*FDisplayClusterTypesConverter::template ToHexString(it->Value.trQuat),
			SerializationDelimiter);
	}

	return result;
}

bool FDisplayClusterVrpnTrackerInputDataHolder::DeserializeFromString(const FString& data)
{
	TArray<FString> parsed;
	data.ParseIntoArray(parsed, SerializationDelimiter);

	if (parsed.Num() % SerializationItems)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Error, TEXT("Wrong items amount after deserialization [%s]"), *data);
		return false;
	}

	for (int i = 0; i < parsed.Num(); i += SerializationItems)
	{
		const int  ch = FCString::Atoi(*parsed[i]);
		const FVector  loc  = FDisplayClusterTypesConverter::template FromHexString<FVector>(parsed[i + 1]);
		const FQuat    quat = FDisplayClusterTypesConverter::template FromHexString<FQuat>(parsed[i + 2]);

		DeviceData.Add(ch, FDisplayClusterVrpnTrackerChannelData{ loc, quat });
	}

	return true;
}

