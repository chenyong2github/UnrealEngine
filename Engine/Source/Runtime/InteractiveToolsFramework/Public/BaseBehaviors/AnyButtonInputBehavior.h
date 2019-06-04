// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehavior.h"
#include "AnyButtonInputBehavior.generated.h"

/**
 * UAnyButtonInputBehavior is a base behavior that provides a generic
 * interface to a TargetButton on a physical Input Device. You can subclass
 * UAnyButtonInputBehavior to write InputBehaviors that can work independent
 * of a particular device type or button, by using the UAnyButtonInputBehavior functions below.
 * 
 * The target device button is selected using the .ButtonNumber property, or you can
 * override the relevant GetXButtonState() function if you need more control.
 * 
 *  @todo spatial controllers
 *  @todo support tablet fingers
 *  @todo support gamepad?
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UAnyButtonInputBehavior : public UInputBehavior
{
	GENERATED_BODY()

public:
	UAnyButtonInputBehavior();

	/** Return set of devices supported by this behavior */
	virtual EInputDevices GetSupportedDevices() override;

	/** @return true if Target Button has been pressed this frame */
	virtual bool IsPressed(const FInputDeviceState& input);

	/** @return true if Target Button is currently held down */
	virtual bool IsDown(const FInputDeviceState& input);

	/** @return true if Target Button was released this frame */
	virtual bool IsReleased(const FInputDeviceState& input);
	
	/** @return current 2D position of Target Device, or zero if device does not have 2D position */
	virtual FVector2D GetClickPoint(const FInputDeviceState& input);

	/** @return current 3D world ray for Target Device position */
	virtual FRay GetWorldRay(const FInputDeviceState& input);
	
	/** @return current 3D world ray and optional 2D position for Target Device */
	virtual FInputDeviceRay GetDeviceRay(const FInputDeviceState& input);

	/** @return last-active supported Device */
	EInputDevices GetActiveDevice() const;

protected:

	/** 
	 * Button number on target device. Button 0 is "default" button on all devices.
	 * Mouse: Left=0, Middle=1, Right=2
	 */
	UPROPERTY()
	int ButtonNumber;


protected:
	/** Which device is currently active */
	EInputDevices ActiveDevice;

	/** @return mouse button state for Target Button */
	virtual FDeviceButtonState GetMouseButtonState(const FInputDeviceState& input);
};