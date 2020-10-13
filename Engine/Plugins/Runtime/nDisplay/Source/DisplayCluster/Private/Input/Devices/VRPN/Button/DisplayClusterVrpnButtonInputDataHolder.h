// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Input/Devices/DisplayClusterInputDeviceTraits.h"
#include "Input/Devices/DisplayClusterInputDeviceBase.h"

class UDisplayClusterConfigurationInputDeviceButton;


/**
 * VRPN button device data holder. Responsible for data serialization and deserialization.
 */
class FDisplayClusterVrpnButtonInputDataHolder
	: public FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnButton>
{
public:
	FDisplayClusterVrpnButtonInputDataHolder(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceButton* CfgDevice);
	virtual ~FDisplayClusterVrpnButtonInputDataHolder();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override;

	virtual FString GetType() const override
	{ return FString("button"); }

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
