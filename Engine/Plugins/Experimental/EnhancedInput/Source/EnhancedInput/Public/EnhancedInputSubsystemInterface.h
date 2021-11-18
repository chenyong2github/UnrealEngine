// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "InputMappingQuery.h"
#include "UObject/Interface.h"

#include "EnhancedInputSubsystemInterface.generated.h"

class APlayerController;
class UInputMappingContext;
class UInputAction;
class UEnhancedPlayerInput;
class UInputModifier;
class UInputTrigger;

// Subsystem interface
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UEnhancedInputSubsystemInterface : public UInterface
{
	GENERATED_BODY()
};

// Includes native functionality shared between all subsystems
class ENHANCEDINPUT_API IEnhancedInputSubsystemInterface
{
	friend class FEnhancedInputModule;

	GENERATED_BODY()

public:

	virtual UEnhancedPlayerInput* GetPlayerInput() const = 0;

	/**
	 * Input simulation via injection. Runs modifiers and triggers delegates as if the input had come through the underlying input system as FKeys.
	 * Applies action modifiers and triggers on top.
	 *
	 * @param Action		The Input Action to set inject input for
	 * @param RawValue		The value to set the action to
	 * @param Modifiers		The modifiers to apply to the injected input.
	 * @param Triggers		The triggers to apply to the injected input.
	 */
	UFUNCTION(BlueprintCallable, Category="Input", meta=(AutoCreateRefTerm="Modifiers,Triggers"))
	virtual void InjectInputForAction(const UInputAction* Action, FInputActionValue RawValue, const TArray<UInputModifier*>& Modifiers, const TArray<UInputTrigger*>& Triggers);

	/**
	 * Remove all applied mapping contexts.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input")
	virtual void ClearAllMappings();

	/**
	 * Add a control mapping context.
	 * @param MappingContext	A set of key to action mappings to apply to this player
	 * @param Priority			Higher priority mappings will be applied first and, if they consume input, will block lower priority mappings.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input", meta=(AdvancedDisplay = "bIgnoreAllPressedKeysUntilRelease"))
	virtual void AddMappingContext(const UInputMappingContext* MappingContext, int32 Priority, const bool bIgnoreAllPressedKeysUntilRelease = true);

	/**
	 * Remove a specific control context. 
	 * This is safe to call even if the context is not applied.
	 * @param MappingContext		Context to remove from the player
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input", meta=(AdvancedDisplay = "bIgnoreAllPressedKeysUntilRelease"))
	virtual void RemoveMappingContext(const UInputMappingContext* MappingContext, const bool bIgnoreAllPressedKeysUntilRelease = true);

	/**
	 * Flag player for reapplication of all mapping contexts at the end of this frame.
	 * This is called automatically when adding or removing mappings contexts.
	 * @param bForceImmediately		THe mapping changes will be applied synchronously, rather than at the end of the frame, making them available to the input system on the same frame.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input", meta=(AdvancedDisplay = "bIgnoreAllPressedKeysUntilRelease"))
	virtual void RequestRebuildControlMappings(bool bForceImmediately = false, const bool bIgnoreAllPressedKeysUntilRelease = true);

	/**
	 * Check if a key mapping is safe to add to a given mapping context within the set of active contexts currently applied to the player controller.
	 * @param InputContext		Mapping context to which the action/key mapping is intended to be added
	 * @param Action			Action that can be triggered by the key
	 * @param Key				Key that will provide input values towards triggering the action
	 * @param OutIssues			Issues that may cause this mapping to be invalid (at your discretion). Any potential issues will be recorded, even if not present in FatalIssues.
	 * @param BlockingIssues	All issues that should be considered fatal as a bitset.
	 * @return					Summary of resulting issues.
	 * @see QueryMapKeyInContextSet
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input|Mapping Queries")
	virtual EMappingQueryResult QueryMapKeyInActiveContextSet(const UInputMappingContext* InputContext, const UInputAction* Action, FKey Key, TArray<FMappingQueryIssue>& OutIssues, EMappingQueryIssue BlockingIssues/* = DefaultMappingIssues::StandardFatal*/);

	/**
	 * Check if a key mapping is safe to add to a collection of mapping contexts
	 * @param PrioritizedActiveContexts	Set of mapping contexts to test against ordered by priority such that earlier entries take precedence over later ones.
	 * @param InputContext		Mapping context to which the action/key mapping is intended to be applied. NOTE: This context must be present in PrioritizedActiveContexts.
	 * @param Action			Action that is triggered by the key
	 * @param Key				Key that will provide input values towards triggering the action
	 * @param OutIssues			Issues that may cause this mapping to be invalid (at your discretion). Any potential issues will be recorded, even if not present in FatalIssues.
	 * @param BlockingIssues	All issues that should be considered fatal as a bitset.
	 * @return					Summary of resulting issues.
	 * @see QueryMapKeyInActiveContextSet
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input|Mapping Queries")
	virtual EMappingQueryResult QueryMapKeyInContextSet(const TArray<UInputMappingContext*>& PrioritizedActiveContexts, const UInputMappingContext* InputContext, const UInputAction* Action, FKey Key, TArray<FMappingQueryIssue>& OutIssues, EMappingQueryIssue BlockingIssues/* = DefaultMappingIssues::StandardFatal*/);

	/**
	 * Check if a mapping context is applied to this subsystem's owner.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input|Mapping Queries")	// TODO: BlueprintPure would be nicer. Move into library?
	virtual bool HasMappingContext(const UInputMappingContext* MappingContext) const;

	/**
	 * Returns the keys mapped to the given action in the active input mapping contexts.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Input|Mapping Queries")
	virtual TArray<FKey> QueryKeysMappedToAction(const UInputAction* Action) const;

private:

	// Forced actions/keys for debug. These will be applied each tick once set even if zeroed, until removed. 
	void ApplyForcedInput(const UInputAction* Action, FInputActionValue Value);
	void ApplyForcedInput(FKey Key, FInputActionValue Value);
	void RemoveForcedInput(const UInputAction* Action);
	void RemoveForcedInput(FKey Key);
	void TickForcedInput(float DeltaTime);

	void InjectChordBlockers(const TArray<int32>& ChordedMappings);
	bool HasTriggerWith(TFunctionRef<bool(const class UInputTrigger*)> TestFn, const TArray<class UInputTrigger*>& Triggers);

	/**
	 * Reapply all control mappings to players pending a rebuild
	 */
	void RebuildControlMappings();

	/** Convert input settings axis config to modifiers for a given mapping */
	void ApplyAxisPropertyModifiers(UEnhancedPlayerInput* PlayerInput, struct FEnhancedActionKeyMapping& Mapping) const;

	TMap<TWeakObjectPtr<const UInputAction>, FInputActionValue> ForcedActions;
	TMap<FKey, FInputActionValue> ForcedKeys;

	/**
	 * A flag that will be set when adding/removing a mapping context.
	 *
	 * If this is true, then any keys that are pressed when control mappings are rebuilt will be ignored 
	 * by the new Input context after being until the key is lifted
	 */
	bool bIgnoreAllPressedKeysUntilReleaseOnRebuild = true;

	bool bMappingRebuildPending = false;

	// Debug visualization implemented in EnhancedInputSubsystemsDebug.cpp
	void ShowDebugInfo(class UCanvas* Canvas);
	void ShowDebugActionModifiers(UCanvas* Canvas, const UInputAction* Action);
	static void PurgeDebugVisualizations();
};