// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "ZoneGraphTypes.h"
#include "ZoneGraphTagConditions.generated.h"

/**
* ZoneGraph Tag condition.
*/
USTRUCT(DisplayName="ZoneGraphTagFilter Compare")
struct MASSAIBEHAVIOR_API FZoneGraphTagFilterCondition : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FZoneGraphTagFilterCondition() : FStateTreeConditionBase() { }

#if WITH_EDITOR
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	virtual bool TestCondition() const override
	{
		return Filter.Pass(Tags) ^ bInvert;
	}

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FZoneGraphTagMask Tags = FZoneGraphTagMask::None;

	UPROPERTY(EditAnywhere, Category = Condition)
	FZoneGraphTagFilter Filter;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
* ZoneGraph Tag condition.
*/
USTRUCT(DisplayName="ZoneGraphTagMask Compare")
struct MASSAIBEHAVIOR_API FZoneGraphTagMaskCondition : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FZoneGraphTagMaskCondition() : FStateTreeConditionBase() { }

#if WITH_EDITOR
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	virtual bool TestCondition() const override
	{
		return Left.CompareMasks(Right, Operator) ^ bInvert;
	}

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FZoneGraphTagMask Left = FZoneGraphTagMask::None;

	UPROPERTY(EditAnywhere, Category = Condition)
	EZoneLaneTagMaskComparison Operator = EZoneLaneTagMaskComparison::Any;
	
	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FZoneGraphTagMask Right = FZoneGraphTagMask::None;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
* ZoneGraph Tag condition.
*/
USTRUCT(DisplayName="ZoneGraphTag Compare")
struct MASSAIBEHAVIOR_API FZoneGraphTagCondition : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FZoneGraphTagCondition() : FStateTreeConditionBase() { }

#if WITH_EDITOR
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	virtual bool TestCondition() const override
	{
		return (Left == Right) ^ bInvert;
	}

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FZoneGraphTag Left = FZoneGraphTag::None;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FZoneGraphTag Right = FZoneGraphTag::None;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};
