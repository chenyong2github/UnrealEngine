// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputTriggers.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"

// Abstract trigger bases
ETriggerState UInputTrigger::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	return IsActuated(ModifiedValue) ? ETriggerState::Triggered : ETriggerState::None;
};

bool UInputTrigger::IsSupportedTriggerEvent(const ETriggerEventsSupported SupportedEvents, const ETriggerEvent Event)
{
	if(SupportedEvents == ETriggerEventsSupported::All)
	{
		return true;
	}
	else if(SupportedEvents == ETriggerEventsSupported::None)
	{
		return false;
	}
	
	// Check the bitmask of SupportedEvent types for each ETriggerEvent
	switch (Event)
	{
	case ETriggerEvent::Started:
		return EnumHasAnyFlags(SupportedEvents, ETriggerEventsSupported::Uninterruptible | ETriggerEventsSupported::Ongoing);
		break;
	case ETriggerEvent::Ongoing:
		return EnumHasAnyFlags(SupportedEvents, ETriggerEventsSupported::Uninterruptible | ETriggerEventsSupported::Ongoing);
		break;
	case ETriggerEvent::Canceled:
		return EnumHasAnyFlags(SupportedEvents, ETriggerEventsSupported::Ongoing);
		break;
	// Triggered can happen from Instant, Overtime, or Cancelable trigger events.
	case ETriggerEvent::Triggered:
		return EnumHasAnyFlags(SupportedEvents, (ETriggerEventsSupported::Instant | ETriggerEventsSupported::Uninterruptible | ETriggerEventsSupported::Ongoing));
		break;
		// Completed is supported by every UInputTrigger
	case ETriggerEvent::Completed:
		return EnumHasAnyFlags(SupportedEvents, ETriggerEventsSupported::All);
		break;
	case ETriggerEvent::None:
	default:
		return false;
	}	
	
	return false;
}

ETriggerState UInputTriggerTimedBase::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	ETriggerState State = ETriggerState::None;

	// Transition to Ongoing on actuation. Update the held duration.
	if (IsActuated(ModifiedValue))
	{
		State = ETriggerState::Ongoing;
		HeldDuration = CalculateHeldDuration(PlayerInput, DeltaTime);
	}
	else
	{
		// Reset duration
		HeldDuration = 0.0f;
	}

	return State;
}

float UInputTriggerTimedBase::CalculateHeldDuration(const UEnhancedPlayerInput* const PlayerInput, const float DeltaTime) const
{
	if (ensureMsgf(PlayerInput, TEXT("No Player Input was given to Calculate with! Returning 1.0")))
	{
		const float TimeDilation = PlayerInput->GetEffectiveTimeDilation();
	
		// Calculates the new held duration, applying time dilation if desired
		return HeldDuration + (!bAffectedByTimeDilation ? DeltaTime : DeltaTime * TimeDilation);
	}

	return 1.0f;
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
	const float TickHeldDuration = CalculateHeldDuration(PlayerInput, DeltaTime);

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

ETriggerState UInputTriggerPulse::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	// Update HeldDuration and derive base state
	ETriggerState State = Super::UpdateState_Implementation(PlayerInput, ModifiedValue, DeltaTime);

	if (State == ETriggerState::Ongoing)
	{
		// If the repeat count limit has not been reached
		if (TriggerLimit == 0 || TriggerCount < TriggerLimit)
		{
			// Trigger when HeldDuration exceeds the interval threshold, optionally trigger on initial actuation
			if (HeldDuration > (Interval * (bTriggerOnStart ? TriggerCount : TriggerCount + 1)))
			{
				++TriggerCount;
				State = ETriggerState::Triggered;
			}
		}
		else
		{
			State = ETriggerState::None;
		}
	}
	else
	{
		// Reset repeat count
		TriggerCount = 0;
	}

	return State;
}


ETriggerState UInputTriggerChordAction::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	// Inherit state from the chorded action
	const FInputActionInstance* EventData = PlayerInput->FindActionInstanceData(ChordAction);
	return EventData ? EventData->TriggerStateTracker.GetState() : ETriggerState::None;
}

UInputTriggerComboAction::UInputTriggerComboAction()
{
	bShouldAlwaysTick = true;
}

ETriggerState UInputTriggerComboAction::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	if (ComboActions.IsEmpty())
	{
		ensureMsgf(false, TEXT("You must add combo actions to the UInputTriggerComboAction for it work properly! Exiting..."));
		return ETriggerState::None;
	}
	
	if (const UInputAction* CurrentAction = ComboActions[CurrentComboStepIndex].ComboStepAction)
	{
		for (const UInputAction* CancelAction : CancelActions)
		{
			if (CancelAction && CancelAction != CurrentAction)
			{
				const FInputActionInstance* CancelState = PlayerInput->FindActionInstanceData(CancelAction);
				if (CancelState && CancelState->GetTriggerEvent() != ETriggerEvent::None)
				{
					// Cancel action firing!
					CurrentComboStepIndex = 0;
					CurrentAction = ComboActions[CurrentComboStepIndex].ComboStepAction;	// Reset for fallthrough
				}
			}
		}

		// Reset if we take too long to hit the next action
		if (CurrentComboStepIndex > 0)
		{
			CurrentTimeBetweenComboSteps += DeltaTime;
			if (CurrentTimeBetweenComboSteps >= ComboActions[CurrentComboStepIndex].TimeToPressKey)
			{
				CurrentComboStepIndex = 0;
				CurrentAction = ComboActions[CurrentComboStepIndex].ComboStepAction;	// Reset for fallthrough			
			}
		}

		const FInputActionInstance* CurrentState = PlayerInput->FindActionInstanceData(CurrentAction);
		if (CurrentState && CurrentState->GetTriggerEvent() == ETriggerEvent::Completed) // + possibly Triggered
		{
			CurrentComboStepIndex++;
			CurrentTimeBetweenComboSteps = 0;
			if (CurrentComboStepIndex >= ComboActions.Num())
			{
				CurrentComboStepIndex = 0;
				return ETriggerState::Triggered;
			}
		}

		if (CurrentComboStepIndex > 0)
		{
			return ETriggerState::Ongoing;
		}
	
		// Really should account for first combo action being mid-trigger...
		const FInputActionInstance* InitialState = PlayerInput->FindActionInstanceData(ComboActions[0].ComboStepAction);
		if (InitialState && InitialState->GetTriggerEvent() > ETriggerEvent::None) // || Cancelled!
		{
			return ETriggerState::Ongoing;
		}
		CurrentTimeBetweenComboSteps = 0;
	}
	return ETriggerState::None;
};