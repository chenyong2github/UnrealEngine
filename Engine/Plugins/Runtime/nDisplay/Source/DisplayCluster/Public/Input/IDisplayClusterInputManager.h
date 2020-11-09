// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IDisplayClusterInputDevice;

/**
 * Available types of input devices
 */
enum EDisplayClusterInputDeviceType
{
	VrpnAnalog = 0,
	VrpnButton,
	VrpnTracker,
	VrpnKeyboard
};


/**
 * Public input manager interface
 */
class IDisplayClusterInputManager
{
public:
	virtual ~IDisplayClusterInputManager() = 0
	{ }

	//////////////////////////////////////////////////////////////////////////
	// Device API
	virtual const IDisplayClusterInputDevice* GetDevice(EDisplayClusterInputDeviceType DeviceType, const FString& DeviceID) const = 0;

	//////////////////////////////////////////////////////////////////////////
	// Device amount
	virtual uint32 GetAxisDeviceAmount()     const = 0;
	virtual uint32 GetButtonDeviceAmount()   const = 0;
	virtual uint32 GetKeyboardDeviceAmount() const = 0;
	virtual uint32 GetTrackerDeviceAmount()  const = 0;

	//////////////////////////////////////////////////////////////////////////
	// Device IDs
	virtual void GetAxisDeviceIds    (TArray<FString>& DeviceIDs) const = 0;
	virtual void GetButtonDeviceIds  (TArray<FString>& DeviceIDs) const = 0;
	virtual void GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) const = 0;
	virtual void GetTrackerDeviceIds (TArray<FString>& DeviceIDs) const = 0;

	//////////////////////////////////////////////////////////////////////////
	// Axes data access
	virtual bool GetAxis(const FString& DeviceID, const int32 axis, float& value) const = 0;

	//////////////////////////////////////////////////////////////////////////
	// Button data access
	virtual bool GetButtonState    (const FString& DeviceID, const int32 Button, bool& CurrentState)    const = 0;
	virtual bool IsButtonPressed   (const FString& DeviceID, const int32 Button, bool& IsPressedCurrently)  const = 0;
	virtual bool IsButtonReleased  (const FString& DeviceID, const int32 Button, bool& IsReleasedCurrently) const = 0;
	virtual bool WasButtonPressed  (const FString& DeviceID, const int32 Button, bool& WasPressed)  const = 0;
	virtual bool WasButtonReleased (const FString& DeviceID, const int32 Button, bool& WasReleased) const = 0;

	//////////////////////////////////////////////////////////////////////////
	// Keyboard data access
	virtual bool GetKeyboardState   (const FString& DeviceID, const int32 Button, bool& CurrentState)    const = 0;
	virtual bool IsKeyboardPressed  (const FString& DeviceID, const int32 Button, bool& IsPressedCurrently)  const = 0;
	virtual bool IsKeyboardReleased (const FString& DeviceID, const int32 Button, bool& IsReleasedCurrently) const = 0;
	virtual bool WasKeyboardPressed (const FString& DeviceID, const int32 Button, bool& WasPressed)  const = 0;
	virtual bool WasKeyboardReleased(const FString& DeviceID, const int32 Button, bool& WasReleased) const = 0;

	//////////////////////////////////////////////////////////////////////////
	// Tracking data access
	virtual bool GetTrackerLocation(const FString& DeviceID, const int32 Tracker, FVector& Location) const = 0;
	virtual bool GetTrackerQuat(const FString& DeviceID,     const int32 Tracker, FQuat& Rotation) const = 0;
};
