// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedActionKeyMapping.h"
#include "GameFramework/PlayerInput.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputTriggers.h"

#include "EnhancedPlayerInput.generated.h"

// Internal representation containing event variants
enum class ETriggerEventInternal : uint8;
enum class EKeyEvent : uint8;

/**
* UEnhancedPlayerInput : UPlayerInput extensions for enhanced player input system
*/
UCLASS(Within = PlayerController, config = Input, transient)
class ENHANCEDINPUT_API UEnhancedPlayerInput : public UPlayerInput
{
	friend class IEnhancedInputSubsystemInterface;
	friend class UEnhancedInputLibrary;
	friend struct FInputTestHelper;

	GENERATED_BODY()

public:

	// TODO: Can we avoid exposing this?
	const FInputActionInstance* FindActionInstanceData(const UInputAction* ForAction) const { return ActionInstanceData.Find(ForAction); }

	/** Retrieve the current value of an action for this player.
	* Note: If the action is not currently triggering this will return a zero value of the appropriate value type, ignoring any ongoing inputs.
	*/
	FInputActionValue GetActionValue(const UInputAction* ForAction) const;

	// Input simulation via injection. Runs modifiers and triggers delegates as if the input had come through the underlying input system as FKeys. Applies action modifiers and triggers on top.
	void InjectInputForAction(const UInputAction* Action, FInputActionValue RawValue, const TArray<UInputModifier*>& Modifiers = {}, const TArray<UInputTrigger*>& Triggers = {});

protected:

	// Applies modifiers and triggers without affecting keys read by the base input system
	virtual void ProcessInputStack(const TArray<UInputComponent*>& InputComponentStack, const float DeltaTime, const bool bGamePaused) override;

	// Causes key to be consumed if it is affecting an action.
	virtual bool IsKeyHandledByAction(FKey Key) const override;

private:

	/** Add a player specific action mapping.
	* Returns index into EnhancedActionMappings array.
	*/
	int32 AddMapping(const FEnhancedActionKeyMapping& Mapping);
	void ClearAllMappings();

	virtual void ConditionalBuildKeyMappings_Internal() const override;

	// Perform a first pass run of modifiers on an action instance
	void InitializeMappingActionModifiers(const FEnhancedActionKeyMapping& Mapping);

	FInputActionValue ApplyModifiers(const TArray<UInputModifier*>& Modifiers, FInputActionValue RawValue, float DeltaTime) const;						// Pre-modified (raw) value
	ETriggerState CalcTriggerState(const TArray<UInputTrigger*>& Triggers, FInputActionValue ModifiedValue, float DeltaTime) const;						// Post-modified value
	ETriggerEventInternal GetTriggerStateChangeEvent(ETriggerState LastTriggerState, ETriggerState NewTriggerState) const;
	ETriggerEvent ConvertInternalTriggerEvent(ETriggerEventInternal Event) const;	// Collapse a detailed internal trigger event into a friendly representation
	void ProcessActionMappingEvent(const UInputAction* Action, float DeltaTime, bool bGamePaused, FInputActionValue RawValue, EKeyEvent KeyEvent, const TArray<UInputModifier*>& Modifiers, const TArray<UInputTrigger*>& Triggers);

	FInputActionInstance& FindOrAddActionEventData(const UInputAction* Action) const;

	template<typename T>
	void GatherActionEventDataForActionMap(const T& ActionMap, TMap<const UInputAction*, FInputActionInstance>& FoundActionEventData) const;

	/** Currently applied key mappings
	 * Note: Source reference only. Use EnhancedActionMappings for the actual mappings (with properly instanced triggers/modifiers)
	 */
	UPROPERTY(Transient)
	TMap<const class UInputMappingContext*, int32> AppliedInputContexts;

	/** This player's version of the Action Mappings */
	UPROPERTY(Transient)
	TArray<FEnhancedActionKeyMapping> EnhancedActionMappings;

	// Number of active binds by key
	TMap<FKey, int32> EnhancedKeyBinds;

	/** Tracked action values. Queryable. */
	UPROPERTY(Transient)
	mutable TMap<const UInputAction*, FInputActionInstance> ActionInstanceData;

	/** Actions which had actuated events at the last call to ProcessInputStack (held/pressed/released) */
	TSet<const UInputAction*> ActionsWithEventsThisTick;

	struct FInjectedInput
	{
		FInputActionValue RawValue;
		TArray<UInputTrigger*> Triggers;
		TArray<UInputModifier*> Modifiers;
	};
	struct FInjectedInputArray
	{
		TArray<FInjectedInput> Injected;
	};

	/** Inputs injected since the last call to ProcessInputStack */
	TMap<const UInputAction*, FInjectedInputArray> InputsInjectedThisTick;

	/** Last frame's injected inputs */
	TSet<const UInputAction*> LastInjectedActions;
};
