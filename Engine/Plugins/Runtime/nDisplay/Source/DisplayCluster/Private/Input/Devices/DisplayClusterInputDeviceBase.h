// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Devices/IDisplayClusterInputDevice.h"
#include "Input/Devices/DisplayClusterInputDeviceTraits.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


/**
 * Abstract input device
 */
template <int DevTypeID>
class FDisplayClusterInputDeviceBase
	: public IDisplayClusterInputDevice
{
public:
	typedef typename display_cluster_input_device_traits<DevTypeID>::dev_channel_data_type   TChannelData;

public:
	FDisplayClusterInputDeviceBase(const FString& InDeviceId, const UDisplayClusterConfigurationInputDevice* CfgDevice)
		: DeviceId(InDeviceId)
	{
		Address = CfgDevice->Address;
		ChannelRemapping = CfgDevice->ChannelRemapping;
	}

	virtual ~FDisplayClusterInputDeviceBase()
	{ }

public:
	virtual bool GetChannelData(const int32 Channel, TChannelData& Data) const
	{
		int32 ChannelToGet = Channel;
		if (const int32* NewChannel = ChannelRemapping.Find(Channel))
		{
			ChannelToGet = *NewChannel;
			UE_LOG(LogDisplayClusterInputVRPN, Verbose, TEXT("DevType %d, channel %d - remapped to channel %d"), DevTypeID, Channel, ChannelToGet);
		}

		const TChannelData* OutData = DeviceData.Find(ChannelToGet);
		if (!OutData)
		{
			UE_LOG(LogDisplayClusterInputVRPN, Verbose, TEXT("%s - channel %d data is not available yet"), *GetId(), ChannelToGet);
			return false;
		}

		Data = *OutData;
		return true;
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual FString GetId() const override
	{ return DeviceId; }

	virtual EDisplayClusterInputDeviceType GetTypeId() const override
	{
		return static_cast<EDisplayClusterInputDeviceType>(DevTypeID);
	}

	virtual void PreUpdate() override
	{ }

	virtual void Update() override
	{ }

	virtual void PostUpdate() override
	{ }

	virtual FString ToString() const override
	{
		return FString::Printf(TEXT("DisplayCluster input device: id=%s, type=%s"), *GetId(), *GetType());
	}

protected:
	// Device ID
	FString DeviceId;
	// Address
	FString Address;
	// Channel remapping
	TMap<int32, int32> ChannelRemapping;
	// Device data
	TMap<int32, TChannelData> DeviceData;
};
