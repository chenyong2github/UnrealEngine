// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Input/Devices/DisplayClusterInputDeviceTraits.h"
#include "Input/Devices/DisplayClusterInputDeviceBase.h"

class UDisplayClusterConfigurationInputDeviceKeyboard;


/**
 * VRPN button device data holder. Responsible for data serialization and deserialization.
 */
class FDisplayClusterVrpnKeyboardInputDataHolder
	: public FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnKeyboard>
{
public:
	FDisplayClusterVrpnKeyboardInputDataHolder(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceKeyboard* CfgDevice);
	virtual ~FDisplayClusterVrpnKeyboardInputDataHolder();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override;

	virtual FString GetType() const override
	{ return FString("keyboard"); }

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
