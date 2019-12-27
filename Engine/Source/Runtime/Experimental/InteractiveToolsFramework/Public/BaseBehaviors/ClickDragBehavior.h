// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/InputBehaviorModifierStates.h"
#include "ClickDragBehavior.generated.h"


/**
 * UClickDragInputBehavior implements a standard "button-click-drag"-style input behavior.
 * An IClickDragBehaviorTarget instance must be provided which is manipulated by this behavior.
 * 
 * The state machine works as follows:
 *    1) on input-device-button-press, call Target::CanBeginClickDragSequence to determine if capture should begin
 *    2) on input-device-move, call Target::OnClickDrag
 *    3) on input-device-button-release, call Target::OnClickRelease
 *    
 * If a ForceEndCapture occurs we call Target::OnTerminateDragSequence   
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UClickDragInputBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:
	UClickDragInputBehavior();

	/**
	 * The modifier set for this behavior
	 */
	FInputBehaviorModifierStates Modifiers;

	/**
	 * Initialize this behavior with the given Target
	 * @param Target implementor of hit-test and on-clicked functions
	 */
	virtual void Initialize(IClickDragBehaviorTarget* Target);


	/**
	 * WantsCapture() will only return capture request if this function returns true (or is null)
	 */
	TFunction<bool(const FInputDeviceState&)> ModifierCheckFunc = nullptr;


	// UInputBehavior implementation

	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& Input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& Input, EInputCaptureSide Side) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& Data) override;
	virtual void ForceEndCapture(const FInputCaptureData& Data) override;


protected:
	/** Click Target object */
	IClickDragBehaviorTarget* Target;

	/**
	 * Internal function that forwards click evens to Target::OnClickPress, you can customize behavior here
	 */
	virtual void OnClickPress(const FInputDeviceState& Input, EInputCaptureSide Side);

	/**
	 * Internal function that forwards click evens to Target::OnClickDrag, you can customize behavior here
	 */
	virtual void OnClickDrag(const FInputDeviceState& Input, const FInputCaptureData& Data);

	/**
	 * Internal function that forwards click evens to Target::OnClickRelease, you can customize behavior here
	 */
	virtual void OnClickRelease(const FInputDeviceState& Input, const FInputCaptureData& Data);
};

