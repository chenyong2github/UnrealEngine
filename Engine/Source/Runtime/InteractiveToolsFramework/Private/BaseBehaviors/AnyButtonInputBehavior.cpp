// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseBehaviors/AnyButtonInputBehavior.h"



UAnyButtonInputBehavior::UAnyButtonInputBehavior()
{
	ButtonNumber = 0;
}


EInputDevices UAnyButtonInputBehavior::GetSupportedDevices()
{
	return EInputDevices::Mouse;
}


bool UAnyButtonInputBehavior::IsPressed(const FInputDeviceState& input)
{
	if (input.IsFromDevice(EInputDevices::Mouse)) 
	{
		ActiveDevice = EInputDevices::Mouse;
		return GetMouseButtonState(input).bPressed;
	} 
	else if (input.IsFromDevice(EInputDevices::TabletFingers))
	{
		ActiveDevice = EInputDevices::TabletFingers;
		//return input.TouchPressed;  // not implemented yet
		return false;
	}
	return false;
}

bool UAnyButtonInputBehavior::IsDown(const FInputDeviceState& input)
{
	if (input.IsFromDevice(EInputDevices::Mouse))
	{
		ActiveDevice = EInputDevices::Mouse;
		return GetMouseButtonState(input).bDown;
	}
	return false;
}

bool UAnyButtonInputBehavior::IsReleased(const FInputDeviceState& input)
{
	if (input.IsFromDevice(EInputDevices::Mouse))
	{
		ActiveDevice = EInputDevices::Mouse;
		return GetMouseButtonState(input).bReleased;
	}
	return false;
}

FVector2D UAnyButtonInputBehavior::GetClickPoint(const FInputDeviceState& input)
{
	if (input.IsFromDevice(EInputDevices::Mouse))
	{
		ActiveDevice = EInputDevices::Mouse;
		return input.Mouse.Position2D;
	}
	return FVector2D::ZeroVector;
}


FRay UAnyButtonInputBehavior::GetWorldRay(const FInputDeviceState& input)
{
	if (input.IsFromDevice(EInputDevices::Mouse))
	{
		ActiveDevice = EInputDevices::Mouse;
		return input.Mouse.WorldRay;
	}
	return FRay(FVector::ZeroVector, FVector(0, 0, 1), true);

}


FInputDeviceRay UAnyButtonInputBehavior::GetDeviceRay(const FInputDeviceState& input)
{
	if (input.IsFromDevice(EInputDevices::Mouse))
	{
		ActiveDevice = EInputDevices::Mouse;
		return FInputDeviceRay(input.Mouse.WorldRay, input.Mouse.Position2D);
	}
	return FInputDeviceRay(FRay(FVector::ZeroVector, FVector(0, 0, 1), true));

}


EInputDevices UAnyButtonInputBehavior::GetActiveDevice() const
{
	return ActiveDevice;
}




FDeviceButtonState UAnyButtonInputBehavior::GetMouseButtonState(const FInputDeviceState& input)
{
	if (ButtonNumber == 2)
	{
		return input.Mouse.Right;
	}
	else if (ButtonNumber == 1)
	{
		return input.Mouse.Middle;
	}
	else
	{
		return input.Mouse.Left;
	}
}