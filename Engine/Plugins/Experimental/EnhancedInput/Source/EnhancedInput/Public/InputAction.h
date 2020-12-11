// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputActionValue.h"
#include "InputMappingQuery.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

#include "InputAction.generated.h"

enum class ETriggerEventInternal : uint8;

// Input action definition. These are instanced per player (via FInputActionInstance)
UCLASS(BlueprintType)
class ENHANCEDINPUT_API UInputAction : public UDataAsset
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	// Track actions that have had their ValueType changed to update blueprints referencing them.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	static TSet<const UInputAction*> ActionsWithModifiedValueTypes;
#endif

	// Should this action swallow any inputs bound to it or allow them to pass through to affect lower priority bound actions?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Action)
	bool bConsumeInput = true;

	// Should this action be able to trigger whilst the game is paused - Replaces bExecuteWhenPaused
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Action)
	bool bTriggerWhenPaused = false;

	// This action's mappings are not intended to be automatically overridden by higher priority context mappings. Users must explicitly remove the mapping first. NOTE: It is the responsibility of the author of the mapping code to enforce this!
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Action)
	bool bReserveAllMappings = false;	// TODO: Need something more complex than this?

	// The type that this action returns from a GetActionValue query or action event
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Action)
	EInputActionValueType ValueType = EInputActionValueType::Boolean;

	/**
	* Trigger qualifiers. If any trigger qualifiers exist the action will not trigger unless:
	* At least one Explicit trigger in this list is be met.
	* All Implicit triggers in this list are met.
	*/
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = Action)
	TArray<UInputTrigger*> Triggers;

	/**
	* Modifiers are applied to the final action value.
	* These are applied sequentially in array order.
	* They are applied on top of any FEnhancedActionKeyMapping modifiers that drove the initial input
	*/
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = Action)
	TArray<UInputModifier*> Modifiers;
};

// Run time queryable action instance
// Generated from UInputAction templates above
USTRUCT(BlueprintType)
struct ENHANCEDINPUT_API FInputActionInstance
{
	friend class UEnhancedPlayerInput;
	friend class UInputTriggerChordAction;

	GENERATED_BODY()

private:
	UPROPERTY()
	const UInputAction* SourceAction = nullptr;

	// Internal trigger states
	ETriggerState LastTriggerState = ETriggerState::None;
	ETriggerState MappingTriggerState = ETriggerState::None;
	ETriggerEventInternal TriggerEventInternal = ETriggerEventInternal(0);	// TODO: Expose access to ETriggerEventInternal?
	bool bMappingTriggerApplied = false;

protected:
	// TODO: Just hold a duplicate of the UInputAction in here?
	// TODO: Restrict blueprint access to triggers and modifiers?
	UPROPERTY(Instanced, BlueprintReadOnly, Category = Config)
	TArray<UInputTrigger*> Triggers;

	UPROPERTY(Instanced, BlueprintReadOnly, Category = Config)
	TArray<UInputModifier*> Modifiers;
	
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to Modifiers."))
	TArray<UInputModifier*> PerInputModifiers_DEPRECATED;
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Moved to Modifiers."))
	TArray<UInputModifier*> FinalValueModifiers_DEPRECATED;

	// Combined value of all inputs mapped to this action
	FInputActionValue Value;

	// Total trigger processing/evaluation time (How long this action has been in event Started, Ongoing, or Triggered
	UPROPERTY(BlueprintReadOnly, Category = Action)
	float ElapsedProcessedTime = 0.f;

	// Triggered time (How long this action has been in event Triggered only)
	UPROPERTY(BlueprintReadOnly, Category = Action)
	float ElapsedTriggeredTime = 0.f;

	// Trigger state
	UPROPERTY(BlueprintReadOnly, Category = Action)
	ETriggerEvent TriggerEvent = ETriggerEvent::None;

public:
	FInputActionInstance() = default;
	FInputActionInstance(const UInputAction* InSourceAction);

	// Current trigger event
	ETriggerEvent GetTriggerEvent() const { return TriggerEvent; }

	// Current action value - Will be zero if the current trigger event is not ETriggerEvent::Triggered!
	FInputActionValue GetValue() const { return TriggerEvent == ETriggerEvent::Triggered ? Value : FInputActionValue(Value.GetValueType(), FInputActionValue::Axis3D::ZeroVector); }

	// Total time the action has been evaluating triggering (Ongoing & Triggered)
	float GetElapsedTime() const { return ElapsedProcessedTime; }

	// Time the action has been actively triggered (Triggered only)
	float GetTriggeredTime() const { return ElapsedTriggeredTime; }

	const TArray<UInputTrigger*>& GetTriggers() const { return Triggers; }
	const TArray<UInputModifier*>& GetModifiers() const { return Modifiers; }

	UE_DEPRECATED(4.26, "GetModifiers(EModifierExecutionPhase) is deprecated. Use GetModifiers()")
	const TArray<UInputModifier*>& GetModifiers(EModifierExecutionPhase ForPhase) const { return Modifiers; }
};