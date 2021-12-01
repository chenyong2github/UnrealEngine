// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "Conditions/StateTreeCondition_Common.h"
#include "GameplayTagContainer.h"
#include "GameplayTagConditions.generated.h"

/**
 * Gameplay Tag match condition.
 */
USTRUCT()
struct STATETREEMODULE_API FGameplayTagMatchConditionInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	FGameplayTagContainer TagContainer;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FGameplayTag Tag;
};

USTRUCT(DisplayName="Gameplay Tag Match")
struct STATETREEMODULE_API FGameplayTagMatchCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	FGameplayTagMatchCondition() = default;
	
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FGameplayTagMatchConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	TStateTreeInstanceDataPropertyHandle<FGameplayTagContainer> TagContainerHandle;
	TStateTreeInstanceDataPropertyHandle<FGameplayTag> TagHandle;
	
	UPROPERTY(EditAnywhere, Category = Condition)
	bool bExactMatch = false;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
* Gameplay Tag Container match condition.
*/
USTRUCT()
struct STATETREEMODULE_API FGameplayTagContainerMatchConditionInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Input)
	FGameplayTagContainer TagContainer;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FGameplayTagContainer OtherContainer;
};

USTRUCT(DisplayName="Gameplay Tag Container Match")
struct STATETREEMODULE_API FGameplayTagContainerMatchCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	FGameplayTagContainerMatchCondition() = default;
	
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FGameplayTagContainerMatchConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	TStateTreeInstanceDataPropertyHandle<FGameplayTagContainer> TagContainerHandle;
	TStateTreeInstanceDataPropertyHandle<FGameplayTagContainer> OtherContainerHandle;

	UPROPERTY(EditAnywhere, Category = Condition)
	EGameplayContainerMatchType MatchType = EGameplayContainerMatchType::Any;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bExactMatch = false;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
* Gameplay Tag Query match condition.
*/
USTRUCT()
struct STATETREEMODULE_API FGameplayTagQueryConditionInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Input)
	FGameplayTagContainer TagContainer;
};

USTRUCT(DisplayName="Gameplay Tag Query")
struct STATETREEMODULE_API FGameplayTagQueryCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	FGameplayTagQueryCondition() = default;
	
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FGameplayTagQueryConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	TStateTreeInstanceDataPropertyHandle<FGameplayTagContainer> TagContainerHandle;

	UPROPERTY(EditAnywhere, Category = Condition)
	FGameplayTagQuery TagQuery;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};