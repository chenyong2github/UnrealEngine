// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Functions required to apply standard "Click" state machines to a target object.
 * See USingleClickBehavior for an example of this kind of state machine.
 */
class IClickBehaviorTarget
{
public:
	virtual ~IClickBehaviorTarget() {}

	/**
	 * Test if target is hit by a click
	 * @param ClickPos device position/ray at click point
	 * @return true if target was hit by click ray/point
	 */
	virtual bool IsHitByClick(const FInputDeviceRay& ClickPos) = 0;


	/**
	 * Notify Target that click ocurred
	 * @param ClickPos device position/ray at click point
	 */
	virtual void OnClicked(const FInputDeviceRay& ClickPos) = 0;
};



/**
 * IHoverBehaviorTarget allows Behaviors to notify Tools/etc about
 * device event data in a generic way, without requiring that all Tools
 * know about the concept of Hovering.
 */
class IHoverBehaviorTarget
{
public:
	virtual ~IHoverBehaviorTarget() {}

	/**
	 * Notify Target about a hover event
	 */
	virtual void OnUpdateHover(const FInputDeviceRay& DevicePos) = 0;
};
