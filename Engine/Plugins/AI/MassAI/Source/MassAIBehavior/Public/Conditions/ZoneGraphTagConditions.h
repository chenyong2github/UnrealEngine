// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "ZoneGraphTypes.h"
#include "ZoneGraphTagConditions.generated.h"

/**
* ZoneGraph Tag condition.
*/

USTRUCT()
struct MASSAIBEHAVIOR_API FZoneGraphTagFilterConditionInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Input)
	FZoneGraphTagMask Tags = FZoneGraphTagMask::None;
};

USTRUCT(DisplayName="ZoneGraphTagFilter Compare")
struct MASSAIBEHAVIOR_API FZoneGraphTagFilterCondition : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FZoneGraphTagFilterCondition() = default;
	
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FZoneGraphTagFilterConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	TStateTreeInstanceDataPropertyHandle<FZoneGraphTagMask> TagsHandle;

	UPROPERTY(EditAnywhere, Category = Condition)
	FZoneGraphTagFilter Filter;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
* ZoneGraph Tag condition.
*/

USTRUCT()
struct MASSAIBEHAVIOR_API FZoneGraphTagMaskConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FZoneGraphTagMask Left = FZoneGraphTagMask::None;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FZoneGraphTagMask Right = FZoneGraphTagMask::None;
};

USTRUCT(DisplayName="ZoneGraphTagMask Compare")
struct MASSAIBEHAVIOR_API FZoneGraphTagMaskCondition : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FZoneGraphTagMaskCondition() = default;

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FZoneGraphTagMaskConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	TStateTreeInstanceDataPropertyHandle<FZoneGraphTagMask> LeftHandle;
	TStateTreeInstanceDataPropertyHandle<FZoneGraphTagMask> RightHandle;

	UPROPERTY(EditAnywhere, Category = Condition)
	EZoneLaneTagMaskComparison Operator = EZoneLaneTagMaskComparison::Any;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
* ZoneGraph Tag condition.
*/

USTRUCT()
struct MASSAIBEHAVIOR_API FZoneGraphTagConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FZoneGraphTag Left = FZoneGraphTag::None;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FZoneGraphTag Right = FZoneGraphTag::None;
};

USTRUCT(DisplayName="ZoneGraphTag Compare")
struct MASSAIBEHAVIOR_API FZoneGraphTagCondition : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FZoneGraphTagCondition() = default;

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FZoneGraphTagConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	TStateTreeInstanceDataPropertyHandle<FZoneGraphTag> LeftHandle;
	TStateTreeInstanceDataPropertyHandle<FZoneGraphTag> RightHandle;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};
