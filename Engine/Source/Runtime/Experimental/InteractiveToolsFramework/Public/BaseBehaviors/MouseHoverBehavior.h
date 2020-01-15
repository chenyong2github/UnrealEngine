// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehavior.h"
#include "InteractiveTool.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/InputBehaviorModifierStates.h"
#include "MouseHoverBehavior.generated.h"



/**
 * Trivial InputBehavior that forwards InputBehavior hover events to a Target object via
 * the IHoverBehaviorTarget interface.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UMouseHoverBehavior : public UInputBehavior
{
	GENERATED_BODY()

public:
	UMouseHoverBehavior();

	/**
	 * The modifier set for this behavior
	 */
	FInputBehaviorModifierStates Modifiers;

	virtual void Initialize(IHoverBehaviorTarget* Target);

	// UInputBehavior hover implementation

	virtual EInputDevices GetSupportedDevices() override;

	virtual bool WantsHoverEvents() override;
	virtual FInputCaptureRequest WantsHoverCapture(const FInputDeviceState& InputState) override;
	virtual FInputCaptureUpdate BeginHoverCapture(const FInputDeviceState& InputState, EInputCaptureSide eSide) override;
	virtual FInputCaptureUpdate UpdateHoverCapture(const FInputDeviceState& InputState) override;
	virtual void EndHoverCapture() override;

protected:
	IHoverBehaviorTarget* Target;
};


