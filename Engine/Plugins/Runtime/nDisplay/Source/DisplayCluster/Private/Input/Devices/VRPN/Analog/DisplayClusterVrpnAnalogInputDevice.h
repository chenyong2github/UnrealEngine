// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Devices/VRPN/Analog/DisplayClusterVrpnAnalogInputDataHolder.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "vrpn/vrpn_Analog.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

class UDisplayClusterConfigurationInputDeviceAnalog;


/**
 * VRPN analog device implementation
 */
class FDisplayClusterVrpnAnalogInputDevice
	: public FDisplayClusterVrpnAnalogInputDataHolder
{
public:
	FDisplayClusterVrpnAnalogInputDevice(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceAnalog* CfgDevice);
	virtual ~FDisplayClusterVrpnAnalogInputDevice();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void Update() override;
	virtual bool Initialize() override;

private:
	// Data update handler
	static void VRPN_CALLBACK HandleAnalogDevice(void *UserData, vrpn_ANALOGCB const AnalogData);

private:
	// The device (PIMPL)
	TUniquePtr<vrpn_Analog_Remote> DevImpl;
};
