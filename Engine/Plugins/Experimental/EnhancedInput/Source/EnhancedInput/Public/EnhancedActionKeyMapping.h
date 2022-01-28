// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"

#include "EnhancedActionKeyMapping.generated.h"

/**
 * A struct that represents player facing mapping options for an action key mapping.
 * Use this to set a unique FName for the mapping option to save it, as well as some FText options
 * for use in UI.
 */
USTRUCT(BlueprintType)
struct FPlayerMappableKeyOptions
{
	GENERATED_BODY()

public:
	
	FPlayerMappableKeyOptions(const UInputAction* InAction = nullptr)
	{
		if(InAction)
		{
			const FString& ActionName = InAction->GetName();
			Name = FName(*ActionName);
			DisplayName = FText::FromString(ActionName);
		}
		else
		{
			Name = NAME_None;
			DisplayName = FText::GetEmpty();
		}
	};

	/** A unique name for this player binding to be saved with. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|PlayerMappable")
	FName Name;
	
	/** The display name that can  */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|PlayerMappable")
	FText DisplayName;

	/** The category that this player binding is in */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|PlayerMappable")
	FText DisplayCategory = FText::GetEmpty();
};

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

	/** If true than this ActionKeyMapping will be exposed as a player bindable key */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|PlayerMappable")
	uint8 bIsPlayerMappable : 1;

	/** Options for making this a player mappable keymapping */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|PlayerMappable", meta = (editCondition = "bIsPlayerBindable"))
	FPlayerMappableKeyOptions PlayerMappableOptions;
	
	/**
	 * If true, then this Key Mapping should be ignored. This is set to true if the key is down
	 * during a rebuild of it's owning PlayerInput ControlMappings.
	 * 
	 * @see IEnhancedInputSubsystemInterface::RebuildControlMappings
	 */
	UPROPERTY(Transient)
	uint8 bShouldBeIgnored : 1;
	
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
		, bIsPlayerMappable(false)
		, PlayerMappableOptions(InAction)
		, bShouldBeIgnored(false)
	{}

};
