// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedPlayerInput.h"
#include "EnhancedInputComponent.h"
#include "InputModifiers.h"
#include "InputTriggers.h"


// NOTE: Enum order represents firing priority(lowest to highest) and is important as multiple keys bound to the same action may generate differing trigger event states.
enum class ETriggerEventInternal : uint8
{
	None,					// No significant trigger state changes occurred
	Completed,				// Triggering stopped after one or more triggered ticks										ETriggerState (Triggered -> None)
	Started,				// Triggering has begun																		ETriggerState (None -> Ongoing)
	Ongoing,				// Triggering is still being processed														ETriggerState (Ongoing -> Ongoing)
	Canceled,				// Triggering has been canceled	mid processing												ETriggerState (Ongoing -> None)
	StartedAndTriggered,	// Triggering occurred in a single tick (fires both started and triggered events)			ETriggerState (None -> Triggered)
	Triggered,				// Triggering occurred after one or more processing ticks									ETriggerState (Ongoing -> Triggered, Triggered -> Triggered)
};

ETriggerEventInternal UEnhancedPlayerInput::GetTriggerStateChangeEvent(ETriggerState LastTriggerState, ETriggerState NewTriggerState) const
{
	// LastTState	NewTState     Event

	// None		 -> Ongoing		= Started
	// None		 -> Triggered	= Started + Triggered
	// Ongoing	 -> None		= Canceled
	// Ongoing	 -> Ongoing		= Ongoing
	// Ongoing	 -> Triggered	= Triggered
	// Triggered -> Triggered	= Triggered
	// Triggered -> None	    = Completed

	switch (LastTriggerState)
	{
	case ETriggerState::None:
		if (NewTriggerState == ETriggerState::Ongoing)
		{
			return ETriggerEventInternal::Started;
		}
		else if (NewTriggerState == ETriggerState::Triggered)
		{
			return ETriggerEventInternal::StartedAndTriggered;
		}
		break;
	case ETriggerState::Ongoing:
		if (NewTriggerState == ETriggerState::None)
		{
			return ETriggerEventInternal::Canceled;
		}
		else if (NewTriggerState == ETriggerState::Ongoing)
		{
			return ETriggerEventInternal::Ongoing;
		}
		else if (NewTriggerState == ETriggerState::Triggered)
		{
			return ETriggerEventInternal::Triggered;
		}
		break;
	case ETriggerState::Triggered:
		if (NewTriggerState == ETriggerState::Triggered)
		{
			return ETriggerEventInternal::Triggered;	// Don't re-raise Started event for multiple completed ticks.
		}
		else if (NewTriggerState == ETriggerState::None)
		{
			return ETriggerEventInternal::Completed;
		}
		break;
	}

	return ETriggerEventInternal::None;
}

ETriggerEvent UEnhancedPlayerInput::ConvertInternalTriggerEvent(ETriggerEventInternal InternalEvent) const
{
	switch (InternalEvent)
	{
	case ETriggerEventInternal::None:
		return ETriggerEvent::None;
	case ETriggerEventInternal::Started:
		return ETriggerEvent::Started;
	case ETriggerEventInternal::Ongoing:
		return ETriggerEvent::Ongoing;
	case ETriggerEventInternal::Canceled:
		return ETriggerEvent::Canceled;
	case ETriggerEventInternal::StartedAndTriggered:
	case ETriggerEventInternal::Triggered:
		return ETriggerEvent::Triggered;
	case ETriggerEventInternal::Completed:
		return ETriggerEvent::Completed;
	}
	return ETriggerEvent::None;
}

enum class EKeyEvent : uint8
{
	None,		// Key did not generate an event this tick and is not being held
	Actuated,	// Key has generated an event this tick
	Held,		// Key generated no event, but is in a held state and wants to continue applying modifiers and triggers
};

FInputActionInstance& UEnhancedPlayerInput::ProcessActionValue(const UInputAction* Action, float DeltaTime, bool bGamePaused, FInputActionValue RawKeyValue, EKeyEvent KeyEvent, ETriggerState& LastTriggerState, const TArray<UInputModifier*>& Modifiers, const TArray<UInputTrigger*>& Triggers)
{
	FInputActionInstance& ActionData = FindOrAddActionEventData(Action);

	// Update values and triggers for all actionable mappings each frame
	ETriggerState TriggerState = ETriggerState::None;

	// Reset action data on the first event processed for the action this tick.
	bool bResetActionData = !ActionsWithEventsThisTick.Contains(Action);

	// If the key state is changing or the key is actuated and being held (and not coming back up this tick) recalculate its value and resulting trigger state.
	if (KeyEvent != EKeyEvent::None)
	{
		if (bResetActionData)
		{
			ActionsWithEventsThisTick.Add(Action);
			ActionData.Value.Reset();	// TODO: what if default value isn't 0 (e.g. bool value with negate modifier). Move reset out to a pre-pass? This may be confusing as triggering requires key interaction for value processing for performance reasons.
		}

		// Apply modifications to the raw value
		EInputActionValueType ValueType = ActionData.Value.GetValueType();
		FInputActionValue ModifiedValue = ApplyModifiers(Modifiers, FInputActionValue(ValueType, RawKeyValue.Get<FVector>()), DeltaTime);
		ModifiedValue = ApplyModifiers(ActionData.PerInputModifiers, ModifiedValue, DeltaTime);
		//UE_CLOG(RawKeyValue.GetMagnitudeSq(), LogTemp, Warning, TEXT("Modified %s -> %s"), *RawKeyValue.ToString(), *ModifiedValue.ToString());

		// Derive a trigger state for this mapping using all applicable triggers
		TriggerState = CalcTriggerState(Triggers, ActionData.Triggers, ModifiedValue, DeltaTime);

		// However, if the game is paused invalidate trigger unless the action allows it. We must still call CalcTriggerState to update internal state.
		// TODO: Potential issues with e.g. hold event that's canceled due to pausing, but jumps straight back to its "triggered" state on unpause if the user continues to hold the key.
		if (bGamePaused && !Action->bTriggerWhenPaused)
		{
			TriggerState = ETriggerState::None;
		}

		// Combine values for active events only, selecting the input with the greatest magnitude for each component in each tick.
		if (TriggerState == ETriggerState::None)
		{
			// No trigger state on an actuated key always modifies (key released/trigger completed, resetting Value)
			ActionData.bInputModifiedValueThisTick |= KeyEvent == EKeyEvent::Actuated;
		}
		else
		{
			const int32 NumComponents = FMath::Max(1, int32(ValueType));
			FVector Modified = ModifiedValue.Get<FVector>();
			FVector Merged = ActionData.Value.Get<FVector>();
			for (int32 Component = 0; Component < NumComponents; ++Component)
			{
				if (FMath::Abs(Modified[Component]) >= FMath::Abs(Merged[Component]))
				{
					Merged[Component] = Modified[Component];
					ActionData.bInputModifiedValueThisTick = true;	// Held keys can modify value only when triggering
				}
			}
			ActionData.Value = FInputActionValue(ValueType, Merged);
		}
	}

	// Use the new trigger state it to determine a trigger event based on changes from the previous trigger state.
	ETriggerEventInternal TriggerEventInternal = GetTriggerStateChangeEvent(LastTriggerState, TriggerState);
	ActionData.TriggerEventInternal = bResetActionData ? TriggerEventInternal : FMath::Max(ActionData.TriggerEventInternal, TriggerEventInternal);
	ActionData.TriggerEvent = ConvertInternalTriggerEvent(ActionData.TriggerEventInternal);

	//UE_CLOG(TriggerState != ETriggerState::None || LastTriggerState != TriggerState, LogTemp, Warning, TEXT("%s - Got trigger state %s - trigger event %s : %.2f"), /**Mapping.Key.ToString(),*/ *Action->GetName(), *UEnum::GetValueAsString(TEXT("EnhancedInput.ETriggerState"), TriggerState), *UEnum::GetValueAsString(TEXT("EnhancedInput.ETriggerEvent"), ActionData.TriggerEvent), 0.f/*ModifiedValue.Get<float>()*/);
	LastTriggerState = TriggerState;

	return ActionData;
}

void UEnhancedPlayerInput::InjectInputForAction(const UInputAction* Action, FInputActionValue RawValue, const TArray<UInputModifier*>& Modifiers, const TArray<UInputTrigger*>& Triggers)
{
	FInjectedInput Input;
	Input.RawValue = RawValue;
	Input.Modifiers = Modifiers;
	Input.Triggers = Triggers;

	InputsInjectedThisTick.FindOrAdd(Action).Injected.Emplace(MoveTemp(Input));
}

void UEnhancedPlayerInput::ProcessInputStack(const TArray<UInputComponent*>& InputComponentStack, const float DeltaTime, const bool bGamePaused)
{
	// We need to grab the down states of all keys before calling Super::ProcessInputStack as it will leave bDownPrevious in the same state as bDown (i.e. this frame, not last).
	static TMap<FKey, bool> KeyDownPrevious;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_KeyDownPrev);
		KeyDownPrevious.Reset();
		KeyDownPrevious.Reserve(GetKeyStateMap().Num());
		for (TPair<FKey, FKeyState>& KeyPair : GetKeyStateMap())
		{
			const FKeyState& KeyState = KeyPair.Value;
			// TODO: Can't just use bDownPrevious as paired axis event edges may not fire due to axial deadzoning/missing axis properties. Need to change how this is detected in PlayerInput.cpp.
			bool bWasDown = KeyState.bDownPrevious || KeyState.EventCounts[IE_Pressed].Num() || KeyState.EventCounts[IE_Repeat].Num();
			KeyDownPrevious.Emplace(KeyPair.Key, bWasDown);
		}
	}

	Super::ProcessInputStack(InputComponentStack, DeltaTime, bGamePaused);

	TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_Main);

	// Process Action bindings
	ActionsWithEventsThisTick.Reset();

	// Handle input devices, applying modifiers and triggers
	for (FEnhancedActionKeyMapping& Mapping : EnhancedActionMappings)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_Devices);

		if (!Mapping.Action)
		{
			continue;
		}

		FKeyState* KeyState = GetKeyState(Mapping.Key);
		FVector RawKeyValue = KeyState ? KeyState->RawValue : FVector::ZeroVector;
		//UE_CLOG(RawKeyValue.SizeSquared(), LogTemp, Warning, TEXT("Key %s - state %s"), *Mapping.Key.GetDisplayName().ToString(), *RawKeyValue.ToString());

		// Establish update type.
		bool bDownLastTick = KeyDownPrevious.FindRef(Mapping.Key);
		// TODO: Can't just use bDown as paired axis event edges may not fire due to axial deadzoning/missing axis properties. Need to change how this is detected in PlayerInput.cpp.
		bool bKeyIsDown = KeyState && (KeyState->bDown || KeyState->EventCounts[IE_Pressed].Num() || KeyState->EventCounts[IE_Repeat].Num());
		bool bKeyIsReleased = !bKeyIsDown && bDownLastTick;
		bool bKeyIsHeld = bKeyIsDown && bDownLastTick;

		EKeyEvent KeyEvent = bKeyIsHeld ? EKeyEvent::Held : ((bKeyIsDown || bKeyIsReleased) ? EKeyEvent::Actuated : EKeyEvent::None);

		// Perform update
		FInputActionInstance& ActionData = ProcessActionValue(Mapping.Action, DeltaTime, bGamePaused, RawKeyValue, KeyEvent, Mapping.LastTickTriggerState, Mapping.Modifiers, Mapping.Triggers);

		// Elapsed timers are tracked per mapping, and combined to give a highest trigger time value.
		Mapping.ElapsedActuatedTime = (Mapping.LastTickTriggerState == ETriggerState::None ? 0.f : Mapping.ElapsedActuatedTime + DeltaTime);
		ActionData.ElapsedProcessedTime = FMath::Max(ActionData.ElapsedProcessedTime, Mapping.ElapsedActuatedTime);
	}


	// Strip stored injected input states that weren't re-injected this tick
	for (auto It = LastInjectedActionState.CreateIterator(); It; ++It)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_InjectedStrip);

		if (!It.Key())
		{
			It.RemoveCurrent();
		}
		else if (!InputsInjectedThisTick.Contains(It.Key()))
		{
			// Reset action state by "releasing the key".
			ProcessActionValue(It.Key(), DeltaTime, bGamePaused, FInputActionValue(), EKeyEvent::Actuated, It.Value().LastTriggerState, {}, {});
			It.RemoveCurrent();
		}
	}

	// Handle injected inputs, applying modifiers and triggers
	for (TPair<const UInputAction*, FInjectedInputArray>& InjectedPair : InputsInjectedThisTick)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_Injected);
		const UInputAction* InjectedAction = InjectedPair.Key;
		if (!InjectedAction)
		{
			continue;
		}

		// Get last injection status data
		FInjectedState* Status = LastInjectedActionState.Find(InjectedAction);
		bool bDownLastTick = Status != nullptr;
		if (!Status)
		{
			Status = &LastInjectedActionState.Emplace(InjectedAction);
		}

		EKeyEvent KeyEvent = bDownLastTick ? EKeyEvent::Held : EKeyEvent::Actuated;
		for (FInjectedInput& InjectedInput : InjectedPair.Value.Injected)
		{
			// Perform update
			FInputActionInstance& ActionData = ProcessActionValue(InjectedAction, DeltaTime, bGamePaused, InjectedInput.RawValue, KeyEvent, Status->LastTriggerState, InjectedInput.Modifiers, InjectedInput.Triggers);

			Status->ElapsedProcessedTime = (Status->LastTriggerState == ETriggerState::None ? 0.f : Status->ElapsedProcessedTime + DeltaTime);
			ActionData.ElapsedProcessedTime = FMath::Max(Status->ElapsedProcessedTime, ActionData.ElapsedProcessedTime);
		}
	}
	InputsInjectedThisTick.Reset();


	// Post tick action instance updates
	for (TPair<const UInputAction*, FInputActionInstance>& ActionPair : ActionInstanceData)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_PostTick);

		FInputActionInstance& ActionData = ActionPair.Value;

		// Apply final value modifiers
		ActionData.Value = ApplyModifiers(ActionData.FinalValueModifiers, ActionData.Value, DeltaTime);

		// If we had our value changed this tick we must update last values for all action level triggers to match the final value.
		if (ActionData.bInputModifiedValueThisTick)
		{
			for (UInputTrigger* Trigger : ActionData.Triggers)
			{
				Trigger->LastValue = ActionData.Value;	// We must use the unfiltered ActionData value here to ensure we update untriggered triggers correctly.
			}
			ActionData.bInputModifiedValueThisTick = false;
		}

		// Evaluate time per action after establishing the internal trigger state across all mappings
		ActionData.ElapsedTriggeredTime += (ActionData.TriggerEvent == ETriggerEvent::Triggered) ? DeltaTime : 0.f;
	}


	// Execute appropriate delegates

	// Cache modifier key states for debug key bindings
	const bool bAlt = IsAltPressed(), bCtrl = IsCtrlPressed(), bShift = IsShiftPressed(), bCmd = IsCmdPressed();

	// TODO: Process APlayerController::InputComponent only!
	// Walk the stack, top to bottom, grabbing actions and firing triggered delegates
	int32 StackIndex = InputComponentStack.Num() - 1;
	for (; StackIndex >= 0; --StackIndex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_Delegates);

		UEnhancedInputComponent* IC = Cast<UEnhancedInputComponent>(InputComponentStack[StackIndex]);
		if (!IC)
		{
			continue;
		}

		// Trigger bound event delegates
		for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : IC->GetActionEventBindings())
		{
			// PERF: Lots of map lookups! Group EnhancedActionBindings by Action?
			if (const FInputActionInstance* ActionData = FindActionInstanceData(Binding->GetAction()))
			{
				// Raise appropriate delegate to report on event state
				if (ActionData->TriggerEvent == Binding->GetTriggerEvent() ||
					(Binding->GetTriggerEvent() == ETriggerEvent::Started && ActionData->TriggerEventInternal == ETriggerEventInternal::StartedAndTriggered))	// Triggering in a single tick should also fire the started event.
				{
					Binding->Execute(*ActionData);
				}
			}
		}

		// Update action value bindings
		for (const FEnhancedInputActionValueBinding& Binding : IC->GetActionValueBindings())
		{
			// PERF: Lots of map lookups! Group EnhancedActionBindings by Action?
			if (const FInputActionInstance* ActionData = FindActionInstanceData(Binding.GetAction()))
			{
				Binding.CurrentValue = ActionData->GetValue();
			}
		}


#if DEV_ONLY_KEY_BINDINGS_AVAILABLE
		// DebugKeyBindings are intended to be used to enable/toggle debug functionality only and have reduced functionality compared to old style key bindings. Limitations/differences include:
		// No support for the 'Any Key' concept. Explicit key binds only.
		// They will always fire, and cannot mask each other or action bindings (i.e. no bConsumeInput option)
		// Chords are supported, but there is no chord masking protection. Exact chord combinations must be met. So a binding of Ctrl + A will not fire if Ctrl + Alt + A is pressed.
		for (const TUniquePtr<FInputDebugKeyBinding>& KeyBinding : IC->GetDebugKeyBindings())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_DebugKeys);

			ensureMsgf(KeyBinding->Chord.Key != EKeys::AnyKey, TEXT("Debug key bindings don't support 'any key'!"));

			// We match modifier key state here to explicitly block unmodified debug actions whilst modifier keys are held down, rather than allow e.g. E through on Alt + E.
			// This acts as a simplified version of chord masking.
			if (KeyBinding->Chord.bAlt == bAlt &&
				KeyBinding->Chord.bCtrl == bCtrl &&
				KeyBinding->Chord.bShift == bShift &&
				KeyBinding->Chord.bCmd == bCmd)
			{
				// TODO: Support full chord masking? Not worth the extra effort for debug keys?
				if (!bGamePaused || KeyBinding->bExecuteWhenPaused)
				{
					FKeyState* KeyState = KeyStateMap.Find(KeyBinding->Chord.Key);
					if (KeyState && KeyState->EventCounts[KeyBinding->KeyEvent].Num() > 0)
					{
						KeyBinding->Execute();
					}
				}
			}
		}
#endif


		// Early termination if this component is blocking input.
		// TODO: Remove support for this?
		if (IC->bBlockInput)
		{
			// stop traversing the stack, all input has been consumed by this InputComponent
			--StackIndex;
			break;
		}
	}

	for (; StackIndex >= 0; --StackIndex)
	{
		if(UEnhancedInputComponent* IC = Cast<UEnhancedInputComponent>(InputComponentStack[StackIndex]))
		{
			for (const FEnhancedInputActionValueBinding& Binding : IC->GetActionValueBindings())
			{
				Binding.CurrentValue.Reset();
			}
		}
	}

	// Reset action instance timers where necessary post delegate calls
	for (TPair<const UInputAction*, FInputActionInstance>& ActionPair : ActionInstanceData)
	{
		FInputActionInstance& ActionData = ActionPair.Value;
		switch (ActionData.TriggerEvent)
		{
		case ETriggerEvent::None:
		case ETriggerEvent::Canceled:
		case ETriggerEvent::Completed:
			ActionData.ElapsedProcessedTime = 0.f;
			break;
		}
		if (ActionData.TriggerEvent != ETriggerEvent::Triggered)
		{
			ActionData.ElapsedTriggeredTime = 0.f;
		}
	}
}

FInputActionValue UEnhancedPlayerInput::GetActionValue(const UInputAction* ForAction) const
{
	const FInputActionInstance* ActionData = FindActionInstanceData(ForAction);
	return ActionData ? ActionData->GetValue() : FInputActionValue(ForAction->ValueType, FInputActionValue::Axis3D::ZeroVector);
}


FEnhancedActionKeyMapping& UEnhancedPlayerInput::AddMapping(const FEnhancedActionKeyMapping& Mapping)
{
	int32 MappingIndex = EnhancedActionMappings.AddUnique(Mapping);
	++EnhancedKeyBinds.FindOrAdd(Mapping.Key);
	bKeyMapsBuilt = false;

	return EnhancedActionMappings[MappingIndex];
}

void UEnhancedPlayerInput::ClearAllMappings()
{
	EnhancedActionMappings.Reset();
	EnhancedKeyBinds.Reset();

	bKeyMapsBuilt = false;
}

template<typename T>
void UEnhancedPlayerInput::GatherActionEventDataForActionMap(const T& ActionMap, TMap<const UInputAction*, FInputActionInstance>& FoundActionEventData) const
{
	for (const typename T::ElementType& Pair : ActionMap)
	{
		const UInputAction* Action = Pair.Key;
		if (FInputActionInstance* ActionData = ActionInstanceData.Find(Action))
		{
			FoundActionEventData.Add(Action, *ActionData);
		}
	}
}

void UEnhancedPlayerInput::ConditionalBuildKeyMappings_Internal() const
{
	Super::ConditionalBuildKeyMappings_Internal();

	// Remove any ActionEventData without a corresponding entry in EnhancedActionMappings or the injection maps
	for (auto Itr = ActionInstanceData.CreateIterator(); Itr; ++Itr)
	{
		const UInputAction* Action = Itr.Key();

		auto HasActionMapping = [&Action](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action; };

		if (!LastInjectedActionState.Contains(Action) &&
			!InputsInjectedThisTick.Contains(Action) &&		// This will be empty for most calls, but could potentially contain data.
			//EngineDefinedActionMappings.ContainsByPredicate(HasActionMapping) && // TODO: EngineDefinedActionMappings are non-rebindable action/key pairings but we have our own systems to handle this...
			!EnhancedActionMappings.ContainsByPredicate(HasActionMapping))
		{
			Itr.RemoveCurrent();
		}
	}

	bKeyMapsBuilt = true;
}

FInputActionValue UEnhancedPlayerInput::ApplyModifiers(const TArray<UInputModifier*>& Modifiers, FInputActionValue RawValue, float DeltaTime) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_Modifiers);

	FInputActionValue ModifiedValue = RawValue;
	for (UInputModifier* Modifier : Modifiers)
	{
		if (Modifier)
		{
			// Enforce that type is kept to RawValue type between modifiers.
			ModifiedValue = FInputActionValue(RawValue.GetValueType(), Modifier->ModifyRaw(this, ModifiedValue, DeltaTime).Get<FInputActionValue::Axis3D>());
		}
	}
	return ModifiedValue;
}

// Calculate a collective representation of trigger state from all key mapping trigger states
ETriggerState UEnhancedPlayerInput::CalcTriggerState(const TArray<UInputTrigger*>& KeyTriggers, const TArray<UInputTrigger*>& ActionTriggers, FInputActionValue ModifiedValue, float DeltaTime) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(EnhPIS_Triggers);

	// Trigger rules by implicit/explicit count:
	// Implicits == 0, Explicits == 0	- Always fire, unless value is 0.
	// Implicits == 0, Explicits  > 0	- At least one explict has fired.
	// Implicits  > 0, Explicits == 0	- All implicits have fired. NOTE: Some implicits firing
	// Implicits  > 0, Explicits  > 0	- All implicits and at least one explicit have fired.
	// Blockers							- Override all other triggers to force trigger failure.

	if (KeyTriggers.Num() + ActionTriggers.Num() == 0)
	{
		// With no triggers the trigger state is represented directly by ModifiedValue.
		return ModifiedValue.IsNonZero() ? ETriggerState::Triggered : ETriggerState::None;
	}

	bool bFoundActiveTrigger = false;	// If any trigger is in an ongoing or triggered state the final state must be at least ongoing (with the exception of blocking triggers!)
	bool bAnyExplictTriggered = false;
	bool bFoundExplicit = false;	// If no explicits are found the trigger may fire through implicit testing only. If explicits exist at least one must be met.
	bool bAllImplicitsTriggered = true;

	auto ConsiderTriggers = [&](const TArray<UInputTrigger*>& Triggers, bool bUpdateLastValue) -> bool	// Returns bIsBlocking
	{
		// TODO: Make this more efficient. Split implicit/explicit to allow us to early out on implicit fail/explicit pass?
		for (UInputTrigger* Trigger : Triggers)
		{
			if (!Trigger)
			{
				continue;
			}

			ETriggerState CurrentState = Trigger->UpdateState(this, ModifiedValue, DeltaTime);

			// Automatically update the last value, avoiding the trigger having to track it, for mapping triggers. Action triggers will perform this step at the end of the tick.
			if (bUpdateLastValue)
			{
				Trigger->LastValue = ModifiedValue;
			}

			switch (Trigger->GetTriggerType())
			{
			case ETriggerType::Explicit:
				bFoundExplicit = true;
				bAnyExplictTriggered |= (CurrentState == ETriggerState::Triggered);
				bFoundActiveTrigger |= CurrentState != ETriggerState::None;
				break;
			case ETriggerType::Implicit:
				bAllImplicitsTriggered &= (CurrentState == ETriggerState::Triggered);
				bFoundActiveTrigger |= CurrentState != ETriggerState::None;
				break;
			case ETriggerType::Blocker:
				if(CurrentState == ETriggerState::Triggered)
				{
					return true;	// Blockers terminate further trigger evaluation
				}
				// Ongoing blockers don't count as active triggers
				break;
			}
		}

		return false;
	};

	if (ConsiderTriggers(KeyTriggers, true) || ConsiderTriggers(ActionTriggers, false))
	{
		// An active Blocker was encountered.
		return ETriggerState::None;
	}

	bool bTriggered = ((!bFoundExplicit || bAnyExplictTriggered) && bAllImplicitsTriggered);
	return bTriggered ? ETriggerState::Triggered : (bFoundActiveTrigger ? ETriggerState::Ongoing : ETriggerState::None);
}

bool UEnhancedPlayerInput::IsKeyHandledByAction(FKey Key) const
{
	// Determines if the key event is handled or not.
	return EnhancedKeyBinds.Contains(Key) || Super::IsKeyHandledByAction(Key);
}

FInputActionInstance& UEnhancedPlayerInput::FindOrAddActionEventData(const UInputAction* Action) const
{
	FInputActionInstance* Instance = ActionInstanceData.Find(Action);
	if (!Instance)
	{
		Instance = &ActionInstanceData.Emplace(Action, FInputActionInstance(Action));
	}
	return *Instance;
}

void UEnhancedPlayerInput::InitializeMappingActionModifiers(const FEnhancedActionKeyMapping& Mapping)
{
#if DO_CHECK
	// Validate no FinalValue modifiers have been applied to the mapping. These will still be processed, but will most likely not have the desired effect.
	// TODO: Strip them out?
	for (UInputModifier* Modifier : Mapping.Modifiers)
	{
		// Final value modifiers should only ever be applied to actions. TODO: Enforce this via the editor!
		checkf(!Modifier || Modifier->GetExecutionPhase() != EModifierExecutionPhase::FinalValue, TEXT("Mapping '%s:%s' is applying a final value modifier '%s', which is only supported on actions."), *Mapping.Action->GetName(), *Mapping.Key.GetFName().ToString(), *Modifier->GetName());
	}
#endif

	if (Mapping.Action)
	{
		// Perform a modifier calculation pass on default data to initialize values correctly.
		FInputActionInstance& EventData = FindOrAddActionEventData(Mapping.Action);
		EventData.Value = ApplyModifiers(Mapping.Modifiers, EventData.Value, 0.f);	// Uses EventData.Value to provide the correct EInputActionValueType
	}
}

