// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Input/Devices/DisplayClusterInputDeviceBase.h"
#include "Input/Devices/DisplayClusterInputDeviceTraits.h"

class UDisplayClusterConfigurationInputDeviceAnalog;


/**
 * VRPN analog device data holder. Responsible for data serialization and deserialization.
 */
class FDisplayClusterVrpnAnalogInputDataHolder
	: public FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnAnalog>
{
public:
	FDisplayClusterVrpnAnalogInputDataHolder(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceAnalog* CfgDevice);
	virtual ~FDisplayClusterVrpnAnalogInputDataHolder();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override;

	virtual FString GetType() const override
	{ return FString("analog"); }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterStringSerializable
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual FString SerializeToString() const override final;
	virtual bool    DeserializeFromString(const FString& Data) override final;

private:
	// Serialization constants
	static constexpr auto SerializationDelimiter = TEXT("@");
	static constexpr auto SerializationItems = 2;
};
