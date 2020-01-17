// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Misc/Change.h"
#include "InputState.h"
#include "InputBehaviorSet.h"
#include "ToolContextInterfaces.h"
#include "InputRouter.generated.h"


/**
 * UInputRouter mediates between a higher-level input event source (eg like an FEdMode)
 * and a set of InputBehaviors that respond to those events. Sets of InputBehaviors are
 * registered, and then PostInputEvent() is called for each event. 
 *
 * Internally one of the active Behaviors may "capture" the event stream.
 * Separate "Left" and "Right" captures are supported, which means that (eg)
 * an independent capture can be tracked for each VR controller.
 *
 * If the input device supports "hover",  PostHoverInputEvent() will forward
 * hover events to InputBehaviors that also support it.
 *
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInputRouter : public UObject
{
	GENERATED_BODY()

protected:
	friend class UInteractiveToolsContext;		// to call Initialize/Shutdown

	UInputRouter();

	/** Initialize the InputRouter with the necessary Context-level state. UInteractiveToolsContext calls this, you should not. */
	virtual void Initialize(IToolsContextTransactionsAPI* TransactionsAPI);

	/** Shutdown the InputRouter. Called by UInteractiveToolsContext. */
	virtual void Shutdown();

public:
	/** Add a new behavior Source. Behaviors from this source will be added to the active behavior set. */
	virtual void RegisterSource(IInputBehaviorSource* Source);

	/** Remove Behaviors from this Source from the active set */
	virtual void DeregisterSource(IInputBehaviorSource* Source);

	/** Add a new InputBehavior to the active behavior set, with optional Source and GroupName */
	virtual void RegisterBehavior(UInputBehavior* Behavior, void* Source = nullptr, const FString& GroupName = "");


	/** Insert a new input event which is used to check for new captures, or forwarded to active capture */
	virtual void PostInputEvent(const FInputDeviceState& Input);

	/** Returns true if there is an active mouse capture */
	virtual bool HasActiveMouseCapture() const;

	// TODO: other capture queries

	/** Insert a new hover input event which is forwarded to all hover-enabled Behaviors */
	virtual void PostHoverInputEvent(const FInputDeviceState& Input);

	/** If this Behavior is capturing, call ForceEndCapture() to notify that we are taking capture away */
	virtual void ForceTerminateSource(IInputBehaviorSource* Source);

	/** Terminate any active captures and end all hovers */
	virtual void ForceTerminateAll();


public:
	/** If true, then we post an Invalidation (ie redraw) request if any active InputBehavior responds to Hover events (default false) */
	UPROPERTY()
	bool bAutoInvalidateOnHover;

	/** If true, then we post an Invalidation (ie redraw) request on every captured input event (default false) */
	UPROPERTY()
	bool bAutoInvalidateOnCapture;


protected:
	IToolsContextTransactionsAPI* TransactionsAPI;

	UPROPERTY()
	UInputBehaviorSet* ActiveInputBehaviors;

	UInputBehavior* ActiveKeyboardCapture = nullptr;
	void* ActiveKeyboardCaptureOwner = nullptr;
	FInputCaptureData ActiveKeyboardCaptureData;

	UInputBehavior* ActiveLeftCapture = nullptr;
	void* ActiveLeftCaptureOwner = nullptr;
	FInputCaptureData ActiveLeftCaptureData;

	UInputBehavior* ActiveRightCapture = nullptr;
	void* ActiveRightCaptureOwner = nullptr;
	FInputCaptureData ActiveRightCaptureData;

	virtual void PostInputEvent_Keyboard(const FInputDeviceState& Input);
	void CheckForKeyboardCaptures(const FInputDeviceState& Input);
	void HandleCapturedKeyboardInput(const FInputDeviceState& Input);

	virtual void PostInputEvent_Mouse(const FInputDeviceState& Input);
	void CheckForMouseCaptures(const FInputDeviceState& Input);
	void HandleCapturedMouseInput(const FInputDeviceState& Input);


	//
	// Hover support
	//

	UInputBehavior* ActiveLeftHoverCapture = nullptr;
	void* ActiveLeftHoverCaptureOwner = nullptr;

	void TerminateHover(EInputCaptureSide Side);
	bool ProcessMouseHover(const FInputDeviceState& Input);
};