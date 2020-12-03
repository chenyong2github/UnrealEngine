// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputTriggers.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "EnhancedPlayerInput.h"


// Abstract trigger bases
ETriggerState UInputTrigger::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	return IsActuated(ModifiedValue) ? ETriggerState::Triggered : ETriggerState::None;
};

ETriggerState UInputTriggerTimedBase::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	ETriggerState State = ETriggerState::None;

	// Transition to Ongoing on actuation.
	if (IsActuated(ModifiedValue))
	{
		State = ETriggerState::Ongoing;
		HeldDuration += DeltaTime;	// TODO: When attached directly to an Action this will tick N times a frame where N is the number of evaluated (actively held) mappings.
	}
	else
	{
		// Reset duration
		HeldDuration = 0.0f;
	}

	return State;
}


// Implementations

ETriggerState UInputTriggerDown::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	// Triggered on down.
	return IsActuated(ModifiedValue) ? ETriggerState::Triggered : ETriggerState::None;
}

ETriggerState UInputTriggerPressed::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	// Triggered on transition to actuated.
	return IsActuated(ModifiedValue) && !IsActuated(LastValue) ? ETriggerState::Triggered : ETriggerState::None;
}

ETriggerState UInputTriggerReleased::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	// Ongoing on hold
	if (IsActuated(ModifiedValue))
	{
		return ETriggerState::Ongoing;
	}

	// Triggered on release
	if (IsActuated(LastValue))
	{
		return ETriggerState::Triggered;
	}

	return ETriggerState::None;
}

ETriggerState UInputTriggerHold::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	// Update HeldDuration and derive base state
	ETriggerState State = Super::UpdateState_Implementation(PlayerInput, ModifiedValue, DeltaTime);

	// Trigger when HeldDuration reaches the threshold
	bool bIsFirstTrigger = !bTriggered;
	bTriggered = HeldDuration >= HoldTimeThreshold;
	if (bTriggered)
	{
		return (bIsFirstTrigger || !bIsOneShot) ? ETriggerState::Triggered : ETriggerState::None;
	}

	return State;
}

ETriggerState UInputTriggerHoldAndRelease::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	// Evaluate the updated held duration prior to calling Super to update the held timer
	// This stops us failing to trigger if the input is released on the threshold frame due to HeldDuration being 0.
	float TickHeldDuration = HeldDuration + DeltaTime;

	// Update HeldDuration and derive base state
	ETriggerState State = Super::UpdateState_Implementation(PlayerInput, ModifiedValue, DeltaTime);

	// Trigger if we've passed the threshold and released
	if (TickHeldDuration >= HoldTimeThreshold && State == ETriggerState::None)
	{
		State = ETriggerState::Triggered;
	}

	return State;
}

ETriggerState UInputTriggerTap::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	float LastHeldDuration = HeldDuration;

	// Updates HeldDuration
	ETriggerState State = Super::UpdateState_Implementation(PlayerInput, ModifiedValue, DeltaTime);

	// Only trigger if pressed then released quickly enough
	if (IsActuated(LastValue) && State == ETriggerState::None && LastHeldDuration < TapReleaseTimeThreshold)
	{
		State = ETriggerState::Triggered;
	}
	else if (HeldDuration >= TapReleaseTimeThreshold)
	{
		// Once we pass the threshold halt all triggering until released
		State = ETriggerState::None;
	}

	return State;
}

ETriggerState UInputTriggerChordAction::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	// Inherit state from the chorded action
	const FInputActionInstance* EventData = PlayerInput->FindActionInstanceData(ChordAction);
	return EventData ? EventData->MappingTriggerState : ETriggerState::None;
};