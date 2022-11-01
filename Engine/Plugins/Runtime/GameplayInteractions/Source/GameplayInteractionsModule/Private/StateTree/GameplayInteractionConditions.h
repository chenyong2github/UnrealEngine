// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayInteractionsTypes.h"
#include "SmartObjectTypes.h"
#include "GameplayTagContainer.h"
#include "GameplayInteractionConditions.generated.h"

class USmartObjectSubsystem;

UENUM()
enum class EGameplayInteractionMatchSlotTagSource : uint8
{
	/** Test slot definition activity Tags. */
	ActivityTags,

	/** Test slot Runtime tags. */
	RuntimeTags,
};

/**
 * Condition to check if Gameplay Tags on a Smart Object slot match the specified tags.
 */
USTRUCT()
struct FGameplayInteractionMatchSlotTagsConditionInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Input")
	FSmartObjectSlotHandle Slot;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FGameplayTagContainer TagsToMatch;
};

USTRUCT(DisplayName="(Gameplay Interaction) Match Slot Tags")
struct FGameplayInteractionSlotTagsMatchCondition : public FGameplayInteractionStateTreeCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FGameplayInteractionMatchSlotTagsConditionInstanceData;

	FGameplayInteractionSlotTagsMatchCondition() = default;

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = "Condition")
	EGameplayInteractionMatchSlotTagSource Source = EGameplayInteractionMatchSlotTagSource::RuntimeTags;

	UPROPERTY(EditAnywhere, Category = "Condition")
	EGameplayContainerMatchType MatchType = EGameplayContainerMatchType::Any;

	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bExactMatch = false;

	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;
	
	/** Handle to retrieve USmartObjectSubsystem. */
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
};


/**
 * Condition to check if Gameplay Tags on a Smart Object slot match the Gameplay Tag query.
 */
USTRUCT()
struct FGameplayInteractionQuerySlotTagsConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Input")
	FSmartObjectSlotHandle Slot;
};

USTRUCT(DisplayName="(Gameplay Interaction) Query Slot Tags")
struct FGameplayInteractionQuerySlotTagCondition : public FGameplayInteractionStateTreeCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FGameplayInteractionQuerySlotTagsConditionInstanceData;

	FGameplayInteractionQuerySlotTagCondition() = default;
	
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = "Condition")
	EGameplayInteractionMatchSlotTagSource Source = EGameplayInteractionMatchSlotTagSource::RuntimeTags;

	UPROPERTY(EditAnywhere, Category = "Condition")
	FGameplayTagQuery TagQuery;

	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;

	/** Handle to retrieve USmartObjectSubsystem. */
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
};


/**
 * Condition to check if a Smart Object slot handle is valid. 
 */
USTRUCT()
struct FGameplayInteractionIsSlotHandleValidConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Input")
	FSmartObjectSlotHandle Slot;
};

USTRUCT(DisplayName="(Gameplay Interaction) Is Slot Handle Valid")
struct FGameplayInteractionIsSlotHandleValidCondition : public FGameplayInteractionStateTreeCondition
{
	GENERATED_BODY()

	using FInstanceDataType = FGameplayInteractionIsSlotHandleValidConditionInstanceData;

	FGameplayInteractionIsSlotHandleValidCondition() = default;
	
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;

	/** Handle to retrieve USmartObjectSubsystem. */
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
};
