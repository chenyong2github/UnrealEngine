// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Math/UnrealMath.h"


/**
 * Input event data can be applicable to many possible input devices.
 * These flags are used to indicate specific or sets of device types.
 */
UENUM()
enum class EInputDevices
{
	None = 0,
	Mouse = 1,
	Gamepad = 2,

	OculusTouch = 4,
	HTCViveWands = 8,
	AnySpatialDevice = OculusTouch | HTCViveWands,

	TabletFingers = 1024
};
ENUM_CLASS_FLAGS(EInputDevices);




/**
 * Current State of a physical device button (mouse, key, etc) at a point in time.
 * Each "click" of a button should involve at minimum two separate state
 * events, one where bPressed=true and one where bReleased=true.
 * Each of these states should occur only once.
 * In addition there may be additional frames where the button is
 * held down and bDown=true and bPressed=false.
 */
struct FDeviceButtonState
{
	/** Was the button pressed down this frame. This should happen once per "click" */
	bool bPressed;
	/** Is the button currently pressed down. This should be true every frame the button is pressed. */
	bool bDown;
	/** Was the button released this frame. This should happen once per "click" */
	bool bReleased;

	FDeviceButtonState()
	{
		bPressed = bDown = bReleased = false;
	}

	/** Update the states of this button */
	void SetStates(bool bPressedIn, bool bDownIn, bool bReleasedIn)
	{
		bPressed = bPressedIn;
		bDown = bDownIn;
		bReleased = bReleasedIn;
	}
};



/**
 * Current State of a physical Mouse device at a point in time.
 */
struct FMouseInputDeviceState
{
	/** State of the left mouse button */
	FDeviceButtonState Left;
	/** State of the middle mouse button */
	FDeviceButtonState Middle;
	/** State of the right mouse button */
	FDeviceButtonState Right;

	/** Change in 'ticks' of the mouse wheel since last state event */
	float WheelDelta;

	/** Current 2D position of the mouse, in application-defined coordinate system */
	FVector2D Position2D;

	/** Change in 2D mouse position from last state event */
	FVector2D Delta2D;

	/** Ray into current 3D scene at current 2D mouse position */
	FRay WorldRay;


	FMouseInputDeviceState()
	{
		Left = FDeviceButtonState();
		Middle = FDeviceButtonState();
		Right = FDeviceButtonState();
		WheelDelta = false;
		Position2D = FVector2D::ZeroVector;
		Delta2D = FVector2D::ZeroVector;
		WorldRay = FRay();
	}
};



/**
 * Current state of physical input devices at a point in time.
 * Assumption is that the state refers to a single physical input device,
 * ie InputDevice field is a single value of EInputDevices and not a combination.
 */
struct FInputDeviceState
{
	/** Which InputDevice member is valid in this state */
	EInputDevices InputDevice;

	//
	// keyboard modifiers
	//

	/** Is they keyboard SHIFT modifier key currently pressed down */
	bool bShiftKeyDown;
	/** Is they keyboard ALT modifier key currently pressed down */
	bool bAltKeyDown;
	/** Is they keyboard CTRL modifier key currently pressed down */
	bool bCtrlKeyDown;
	/** Is they keyboard CMD modifier key currently pressed down (only on Apple devices) */
	bool bCmdKeyDown;

	/** Current state of Mouse device, if InputDevice == EInputDevices::Mouse */
	FMouseInputDeviceState Mouse;


	FInputDeviceState() 
	{
		InputDevice = EInputDevices::None;
		bShiftKeyDown = bAltKeyDown = bCtrlKeyDown = bCmdKeyDown = false;
		Mouse = FMouseInputDeviceState();
	}

	/** Update keyboard modifier key states */
	void SetKeyStates(bool bShiftDown, bool bAltDown, bool bCtrlDown, bool bCmdDown) 
	{
		bShiftKeyDown = bShiftDown;
		bAltKeyDown = bAltDown;
		bCtrlKeyDown = bCtrlDown;
		bCmdKeyDown = bCmdDown;
	}

	/**
	 * @param DeviceType Combination of device-type flags
	 * @return true if this input state is for an input device that matches the query flags 
	 */
	bool IsFromDevice(EInputDevices DeviceType) const
	{
		return ((InputDevice & DeviceType) != EInputDevices::None);
	}
};

