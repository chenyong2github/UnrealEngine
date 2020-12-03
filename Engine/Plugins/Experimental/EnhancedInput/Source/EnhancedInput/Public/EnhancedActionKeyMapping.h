// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"

#include "EnhancedActionKeyMapping.generated.h"

/**
 * Defines a mapping between a key activation and the resulting enhanced action
 * An key could be a button press, joystick axis movement, etc.
 * An enhanced action could be MoveForward, Jump, Fire, etc.
 *
**/
USTRUCT(BlueprintType)
struct FEnhancedActionKeyMapping
{
	GENERATED_BODY()

	/** Action to be affected by the key  */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	const UInputAction* Action = nullptr;

	/** Key that affect the action. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	FKey Key;

	//ControllerId ControllerId;	// TODO: Controller id/player id (hybrid?) allowing binding multiple pads to a series of actions.

	/**
	* Trigger qualifiers. If any trigger qualifiers exist the mapping will not trigger unless:
	* If there are any Explicit triggers in this list at least one of them must be met.
	* All Implicit triggers in this list must be met.
	*/
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Input")
	TArray<class UInputTrigger*> Triggers;

	/**
	* Modifiers applied to the raw key value.
	* These are applied sequentially in array order.
	*/
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Input")
	TArray<class UInputModifier*> Modifiers;

	bool operator==(const FEnhancedActionKeyMapping& Other) const
	{
		return (Action == Other.Action &&
				Key == Other.Key &&
				Triggers == Other.Triggers &&
				Modifiers == Other.Modifiers);
	}

	FEnhancedActionKeyMapping(const UInputAction* InAction = nullptr, const FKey InKey = EKeys::Invalid)
		: Action(InAction)
		, Key(InKey)
	{}

};
