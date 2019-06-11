// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/**
 * IModifierToggleBehaviorTarget is an interface that InputBehaviors can use to notify
 * a target about modifier toggle states (like shift key being down, etc).
 * The meaning of the modifier ID is client-defined (generally provided to the InputBehavior in a registration step)
 */
class IModifierToggleBehaviorTarget
{
public:
	virtual ~IModifierToggleBehaviorTarget() {}

	/**
	 * Notify target of current modifier state
	 * @param ModifierID client-defined integer that identifiers modifier
	 * @param bIsOn is modifier current on or off
	 */
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn)
	{
	}
};


/**
 * Functions required to apply standard "Click" state machines to a target object.
 * See USingleClickBehavior for an example of this kind of state machine.
 */
class IClickBehaviorTarget : public IModifierToggleBehaviorTarget
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
 * Functions required to apply standard "Click-Drag" state machines to a target object.
 * See UClickDragBehavior for an example of this kind of state machine.
 */
class IClickDragBehaviorTarget : public IModifierToggleBehaviorTarget
{
public:
	virtual ~IClickDragBehaviorTarget() {}

	/**
	 * Test if target can begin click-drag interaction at this point
	 * @param PressPos device position/ray at click point
	 * @return true if target wants to begin sequence
	 */
	virtual bool CanBeginClickDragSequence(const FInputDeviceRay& PressPos) = 0;


	/**
	 * Notify Target that click press ocurred
	 * @param PressPos device position/ray at click point
	 */
	virtual void OnClickPress(const FInputDeviceRay& PressPos) = 0;

	/**
	 * Notify Target that input position has changed
	 * @param DragPos device position/ray at click point
	 */
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) = 0;

	/**
	 * Notify Target that click release occurred
	 * @param ReleasePos device position/ray at click point
	 */
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) = 0;

	/**
	 * Notify Target that click-drag sequence has been explicitly terminated (eg by escape key)
	 */
	virtual void OnTerminateDragSequence() = 0;
};






/**
 * Target interface used by InputBehaviors that want to implement a multi-click sequence
 * (eg such as drawing a polygon with multiple clicks)
 */
class IClickSequenceBehaviorTarget : public IModifierToggleBehaviorTarget
{
public:
	virtual ~IClickSequenceBehaviorTarget() {}

	/**
	 * Notify Target device position has changed but a click sequence hasn't begun yet (eg for interactive previews)
	 * @param ClickPos device position/ray at click point
	 */
	virtual void OnBeginSequencePreview(const FInputDeviceRay& ClickPos) { }

	/**
	 * Test if target would like to begin sequence based on this click
	 * @param ClickPos device position/ray at click point
	 * @return true if target wants to begin sequence
	 */
	virtual bool CanBeginClickSequence(const FInputDeviceRay& ClickPos) = 0;

	/**
	 * Notify Target that click sequence can begin at click point
	 * @param ClickPos device position/ray at click point
	 */
	virtual void OnBeginClickSequence(const FInputDeviceRay& ClickPos) = 0;
	
	/**
	 * Notify Target device position has changed but a click hasn't ocurred yet (eg for interactive previews)
	 * @param ClickPos device position/ray at click point
	 */
	virtual void OnNextSequencePreview(const FInputDeviceRay& ClickPos) { }

	/**
	 * Notify Target about next click in sqeuence
	 * @param ClickPos device position/ray at click point
	 * @return false if Target wants to terminate sequence
	 */
	virtual bool OnNextSequenceClick(const FInputDeviceRay& ClickPos) = 0;

	/**
	 * Notify Target that click sequence has been explicitly terminated (eg by escape key, cancel tool, etc)
	 */
	virtual void OnTerminateClickSequence() = 0;

	/**
	 * Target overrides this and returns true if it wants to abort click sequence.
	 * Behavior checks every update and if this ever returns true, terminates sequence
	 */
	virtual bool RequestAbortClickSequence() { return false; }
};





/**
 * IHoverBehaviorTarget allows Behaviors to notify Tools/etc about
 * device event data in a generic way, without requiring that all Tools
 * know about the concept of Hovering.
 */
class IHoverBehaviorTarget : public IModifierToggleBehaviorTarget
{
public:
	virtual ~IHoverBehaviorTarget() {}

	/**
	 * Notify Target about a hover event
	 */
	virtual void OnUpdateHover(const FInputDeviceRay& DevicePos) = 0;
};
