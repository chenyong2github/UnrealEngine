// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterVrpnKeyboardInputDevice.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"


FDisplayClusterVrpnKeyboardInputDevice::FDisplayClusterVrpnKeyboardInputDevice(const FString& DeviceId, const UDisplayClusterConfigurationInputDeviceKeyboard* CfgDevice)
	: FDisplayClusterVrpnKeyboardInputDataHolder(DeviceId, CfgDevice)
{
}

FDisplayClusterVrpnKeyboardInputDevice::~FDisplayClusterVrpnKeyboardInputDevice()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterVrpnKeyboardInputDevice::PreUpdate()
{
	// Update 'old' states before calling mainloop
	for (auto it = DeviceData.CreateIterator(); it; ++it)
	{
		it->Value.BtnStateOld = it->Value.BtnStateNew;
	}
}

void FDisplayClusterVrpnKeyboardInputDevice::Update()
{
	if (DevImpl)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Verbose, TEXT("Updating device: %s"), *GetId());
		DevImpl->mainloop();
	}
}

bool FDisplayClusterVrpnKeyboardInputDevice::Initialize()
{
	// Instantiate device implementation
	DevImpl.Reset(new vrpn_Button_Remote(TCHAR_TO_UTF8(*Address)));
	// Register update handler
	if(DevImpl->register_change_handler(this, &FDisplayClusterVrpnKeyboardInputDevice::HandleKeyboardDevice) != 0)
	{
		UE_LOG(LogDisplayClusterInputVRPN, Error, TEXT("%s - couldn't register VRPN change handler"), *ToString());
		return false;
	}

	// Base initialization
	return FDisplayClusterVrpnKeyboardInputDataHolder::Initialize();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterVrpnKeyboardInputDevice
//////////////////////////////////////////////////////////////////////////////////////////////
void VRPN_CALLBACK FDisplayClusterVrpnKeyboardInputDevice::HandleKeyboardDevice(void *UserData, vrpn_BUTTONCB const ButtonData)
{
	auto Dev = reinterpret_cast<FDisplayClusterVrpnKeyboardInputDevice*>(UserData);
	
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
	UE_LOG(LogDisplayClusterInputVRPN, VeryVerbose, TEXT("Keyboard %s:%d - %d"), *Dev->GetId(), ButtonData.button, ButtonData.state);
}
