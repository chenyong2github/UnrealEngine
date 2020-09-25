// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Devices/VRPN/Button/DisplayClusterVrpnButtonInputDataHolder.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "vrpn/vrpn_Button.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

class UDisplayClusterConfigurationInputDeviceButton;


/**
 * VRPN button device implementation
 */
class FDisplayClusterVrpnButtonInputDevice
	: public FDisplayClusterVrpnButtonInputDataHolder
{
public:
	FDisplayClusterVrpnButtonInputDevice(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceButton* CfgDevice);
	virtual ~FDisplayClusterVrpnButtonInputDevice();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterInputDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void PreUpdate() override;
	virtual void Update() override;
	virtual bool Initialize() override;

private:
	// Data update handler
	static void VRPN_CALLBACK HandleButtonDevice(void *UserData, vrpn_BUTTONCB const ButtonData);

private:
	// The device (PIMPL)
	TUniquePtr<vrpn_Button_Remote> DevImpl;
};
