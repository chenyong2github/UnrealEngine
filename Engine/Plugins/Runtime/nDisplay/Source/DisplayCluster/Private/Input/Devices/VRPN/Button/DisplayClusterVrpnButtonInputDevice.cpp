// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/Devices/VRPN/Button/DisplayClusterVrpnButtonInputDevice.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


FDisplayClusterVrpnButtonInputDevice::FDisplayClusterVrpnButtonInputDevice(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceButton* CfgDevice)
	: FDisplayClusterVrpnButtonInputDataHolder(DeviceId, CfgDevice)
{
}

FDisplayClusterVrpnButtonInputDevice::~FDisplayClusterVrpnButtonInputDevice()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterVrpnButtonInputDevice::PreUpdate()
{
	// Update 'old' states before calling mainloop
	for (auto it = DeviceData.CreateIterator(); it; ++it)
	{
		it->Value.BtnStateOld = it->Value.BtnStateNew;
	}
}

void FDisplayClusterVrpnButtonInputDevice::Update()
{
	if (DevImpl)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Verbose, TEXT("Updating device: %s"), *GetId());
		DevImpl->mainloop();
	}
}

bool FDisplayClusterVrpnButtonInputDevice::Initialize()
{
	// Instantiate device implementation
	DevImpl.Reset(new vrpn_Button_Remote(TCHAR_TO_UTF8(*Address)));
	// Register update handler
	if(DevImpl->register_change_handler(this, &FDisplayClusterVrpnButtonInputDevice::HandleButtonDevice) != 0)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Error, TEXT("%s - couldn't register VRPN change handler"), *ToString());
		return false;
	}

	// Base initialization
	return FDisplayClusterVrpnButtonInputDataHolder::Initialize();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterVrpnButtonInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
void VRPN_CALLBACK FDisplayClusterVrpnButtonInputDevice::HandleButtonDevice(void *UserData, vrpn_BUTTONCB const ButtonData)
{
	auto Dev = reinterpret_cast<FDisplayClusterVrpnButtonInputDevice*>(UserData);
	
	auto Item = Dev->DeviceData.Find(ButtonData.button);
	if (!Item)
	{
		Item = &Dev->DeviceData.Add(ButtonData.button);
		// Explicit initial old state set
		Item->BtnStateOld = false;
	}

	//@note: Actually the button can change state for several time during one update cycle. For example
	//       it could change 0->1->0. Then we will send only the latest state and as a result the state
	//       change won't be processed. I don't process such situations because it's not ok if button
	//       changes the state so quickly. It's probably a contact shiver or something else. Normal button
	//       usage will lead to state change separation between update frames.


	// Convert button state from int to bool here. Actually VRPN has only two states for
	// buttons (0-released, 1-pressed) but still uses int32 type for the state.
	Item->BtnStateNew = (ButtonData.state != 0);
	UE_LOG(LogDisplayClusterInputVRPN, VeryVerbose, TEXT("Button %s:%d - %d"), *Dev->GetId(), ButtonData.button, ButtonData.state);
}
