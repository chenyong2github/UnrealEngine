// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehavior.h"
#include "InteractiveTool.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/InputBehaviorModifierStates.h"
#include "KeyAsModifierInputBehavior.generated.h"


/**
 * UKeyAsModifierInputBehavior converts a specific key press/release into
 * a "Modifier" toggle via the IModifierToggleBehaviorTarget interface
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UKeyAsModifierInputBehavior : public UInputBehavior
{
	GENERATED_BODY()

public:
	UKeyAsModifierInputBehavior();

	virtual EInputDevices GetSupportedDevices() override
	{
		return EInputDevices::Keyboard;
	}

	/**
	 * Initialize this behavior with the given Target
	 * @param Target implementor of modifier-toggle behavior
	 * @param ModifierID integer ID that identifiers the modifier toggle
	 * @param ModifierKey the key that will be used as the modifier toggle
	 */
	virtual void Initialize(IModifierToggleBehaviorTarget* Target, int ModifierID, const FKey& ModifierKey);

	/**
	 * WantsCapture() will only return capture request if this function returns true (or is null)
	 * Intended to be used for alt/ctrl/cmd/shift modifiers on the main ModifierKey
	 */
	TFunction<bool(const FInputDeviceState&)> ModifierCheckFunc = nullptr;

	// UInputBehavior implementation

	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& Input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& Input, EInputCaptureSide Side) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& Data) override;
	virtual void ForceEndCapture(const FInputCaptureData& Data) override;


protected:
	/** Modifier Target object */
	IModifierToggleBehaviorTarget* Target;

	/** Key that is used as modifier */
	FKey ModifierKey;

	/** Modifier set for this behavior, internally initialized with check on ModifierKey */
	FInputBehaviorModifierStates Modifiers;

	/** The key that was pressed to activate capture */
	FKey PressedButton;
};
