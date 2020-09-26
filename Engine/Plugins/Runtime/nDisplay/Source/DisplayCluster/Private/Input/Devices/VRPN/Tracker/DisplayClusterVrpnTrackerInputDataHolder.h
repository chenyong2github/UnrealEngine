// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Input/Devices/DisplayClusterInputDeviceTraits.h"
#include "Input/Devices/DisplayClusterInputDeviceBase.h"

class UDisplayClusterConfigurationInputDeviceTracker;


/**
 * VRPN tracker device data holder. Responsible for data serialization and deserialization.
 */
class FDisplayClusterVrpnTrackerInputDataHolder
	: public FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnTracker>
{
public:
	FDisplayClusterVrpnTrackerInputDataHolder(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceTracker* CfgDevice);
	virtual ~FDisplayClusterVrpnTrackerInputDataHolder();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override;

	virtual FString GetType() const override
	{ return FString("tracker"); }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterStringSerializable
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual FString SerializeToString() const override final;
	virtual bool    DeserializeFromString(const FString& Data) override final;

private:
	// Serialization constants
	static constexpr auto SerializationDelimiter = TEXT("@");
	static constexpr auto SerializationItems = 3;
};
