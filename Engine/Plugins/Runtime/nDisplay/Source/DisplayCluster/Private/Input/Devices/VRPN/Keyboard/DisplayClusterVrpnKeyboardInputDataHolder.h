// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Devices/DisplayClusterInputDeviceTraits.h"
#include "Input/Devices/DisplayClusterInputDeviceBase.h"

#include "CoreMinimal.h"


/**
 * VRPN button device data holder. Responsible for data serialization and deserialization.
 */
class FDisplayClusterVrpnKeyboardInputDataHolder
	: public FDisplayClusterInputDeviceBase<EDisplayClusterInputDeviceType::VrpnKeyboard>
{
public:
	FDisplayClusterVrpnKeyboardInputDataHolder(const FDisplayClusterConfigInput& Config);
	virtual ~FDisplayClusterVrpnKeyboardInputDataHolder();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override;

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
