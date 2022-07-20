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
class UInputMappingContext;

/**
* UEnhancedPlayerInput : UPlayerInput extensions for enhanced player input system
*/
UCLASS(config = Input, transient)
class ENHANCEDINPUT_API UEnhancedPlayerInput : public UPlayerInput
{
	friend class IEnhancedInputSubsystemInterface;
	friend class UEnhancedInputLibrary;
	friend struct FInputTestHelper;

	GENERATED_BODY()

public:

	/**
	* Returns the action instance data for the given input action if there is any. Returns nullptr if the action is not available.
	*/
	const FInputActionInstance* FindActionInstanceData(const UInputAction* ForAction) const { return ActionInstanceData.Find(ForAction); }

	/** Retrieve the current value of an action for this player.
	* Note: If the action is not currently triggering this will return a zero value of the appropriate value type, ignoring any ongoing inputs.
	*/
	FInputActionValue GetActionValue(const UInputAction* ForAction) const;

	// Input simulation via injection. Runs modifiers and triggers delegates as if the input had come through the underlying input system as FKeys. Applies action modifiers and triggers on top.
	void InjectInputForAction(const UInputAction* Action, FInputActionValue RawValue, const TArray<UInputModifier*>& Modifiers = {}, const TArray<UInputTrigger*>& Triggers = {});

	virtual bool InputKey(const FInputKeyParams& Params) override;
	
	// Applies modifiers and triggers without affecting keys read by the base input system
	virtual void ProcessInputStack(const TArray<UInputComponent*>& InputComponentStack, const float DeltaTime, const bool bGamePaused) override;

	/** Returns the Time Dilation value that is currently effecting this input. */
	float GetEffectiveTimeDilation() const;
	
protected:
	
	// Causes key to be consumed if it is affecting an action.
	virtual bool IsKeyHandledByAction(FKey Key) const override;
	
	/** Note: Source reference only. Use EnhancedActionMappings for the actual mappings (with properly instanced triggers/modifiers) */
	const TMap<TObjectPtr<const UInputMappingContext>, int32>& GetAppliedInputContexts() const { return AppliedInputContexts; }

	/** This player's version of the Action Mappings */
	const TArray<FEnhancedActionKeyMapping>& GetEnhancedActionMappings() const { return EnhancedActionMappings; }
	

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
	TMap<TObjectPtr<const UInputMappingContext>, int32> AppliedInputContexts;

	/** This player's version of the Action Mappings */
	UPROPERTY(Transient)
	TArray<FEnhancedActionKeyMapping> EnhancedActionMappings;

	// Number of active binds by key
	TMap<FKey, int32> EnhancedKeyBinds;

	/** Tracked action values. Queryable. */
	UPROPERTY(Transient)
	mutable TMap<TObjectPtr<const UInputAction>, FInputActionInstance> ActionInstanceData;

	/** Actions which had actuated events at the last call to ProcessInputStack (held/pressed/released) */
	TSet<const UInputAction*> ActionsWithEventsThisTick;

	/**
	 * A map of Keys to the amount they were depressed this frame. This is reset with each call to ProcessInputStack
	 * and is populated within UEnhancedPlayerInput::InputKey.
	 */
	UPROPERTY(Transient)
	TMap<FKey, FVector> KeysPressedThisTick;

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

	/** The last time of the last frame that was processed in ProcessPlayerInput */
	float LastFrameTime = 0.0f;

	/** Delta seconds between frames calculated with UWorld::GetRealTimeSeconds */
	float RealTimeDeltaSeconds = 0.0f;
};
