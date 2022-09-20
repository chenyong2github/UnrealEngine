// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AITypes.h"
#include "InstancedStruct.h"
#include "StateTreeConditionBase.h"
#include "StateTreeAnyEnum.h"
#include "StateTreeCommonConditions.generated.h"

/**
 * Condition comparing two integers.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCompareIntConditionInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Input)
	int32 Left = 0;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 Right = 0;
};
STATETREE_POD_INSTANCEDATA(FStateTreeCompareIntConditionInstanceData);

USTRUCT(DisplayName="Integer Compare")
struct STATETREEMODULE_API FStateTreeCompareIntCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCompareIntConditionInstanceData InstanceDataType;

	FStateTreeCompareIntCondition() = default;
	explicit FStateTreeCompareIntCondition(const EGenericAICheck InOperator, const EStateTreeCompare InInverts = EStateTreeCompare::Default)
		: bInvert(InInverts == EStateTreeCompare::Invert)
		, Operator(InOperator)
	{}

	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCompareIntConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EGenericAICheck Operator = EGenericAICheck::Equal;
};

/**
 * Condition comparing two floats.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCompareFloatConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	double Left = 0.0;

	UPROPERTY(EditAnywhere, Category = Parameter)
	double Right = 0.0;
};
STATETREE_POD_INSTANCEDATA(FStateTreeCompareFloatConditionInstanceData);

USTRUCT(DisplayName = "Float Compare")
struct STATETREEMODULE_API FStateTreeCompareFloatCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCompareFloatConditionInstanceData InstanceDataType;

	FStateTreeCompareFloatCondition() = default;
	explicit FStateTreeCompareFloatCondition(const EGenericAICheck InOperator, const EStateTreeCompare InInverts = EStateTreeCompare::Default)
		: bInvert(InInverts == EStateTreeCompare::Invert)
		, Operator(InOperator)
	{}

	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCompareFloatConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EGenericAICheck Operator = EGenericAICheck::Equal;
};

/**
 * Condition comparing two booleans.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCompareBoolConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	bool bLeft = false;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bRight = false;
};
STATETREE_POD_INSTANCEDATA(FStateTreeCompareBoolConditionInstanceData);

USTRUCT(DisplayName = "Bool Compare")
struct STATETREEMODULE_API FStateTreeCompareBoolCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCompareBoolConditionInstanceData InstanceDataType;

	FStateTreeCompareBoolCondition() = default;
	explicit FStateTreeCompareBoolCondition(const EStateTreeCompare InInverts)
		: bInvert(InInverts == EStateTreeCompare::Invert)
	{}

	FStateTreeCompareBoolCondition(const bool bInInverts)
		: bInvert(bInInverts)
	{}

	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCompareBoolConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;
};

/**
 * Condition comparing two enums.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCompareEnumConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FStateTreeAnyEnum Left;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FStateTreeAnyEnum Right;
};
STATETREE_POD_INSTANCEDATA(FStateTreeCompareEnumConditionInstanceData);

USTRUCT(DisplayName = "Enum Compare")
struct STATETREEMODULE_API FStateTreeCompareEnumCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCompareEnumConditionInstanceData InstanceDataType;

	FStateTreeCompareEnumCondition() = default;
	explicit FStateTreeCompareEnumCondition(const EStateTreeCompare InInverts)
		: bInvert(InInverts == EStateTreeCompare::Invert)
	{}

	FStateTreeCompareEnumCondition(const bool bInInverts)
		: bInvert(bInInverts)
	{}

	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCompareEnumConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual void OnBindingChanged(const FGuid& ID, FStateTreeDataView InstanceData, const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup) override;
#endif

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;
};

/**
 * Condition comparing distance between two vectors.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCompareDistanceConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FVector Source = FVector(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = Parameter)
	FVector Target = FVector(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = Parameter)
	double Distance = 0.0;
};
STATETREE_POD_INSTANCEDATA(FStateTreeCompareDistanceConditionInstanceData);

USTRUCT(DisplayName = "Distance Compare")
struct STATETREEMODULE_API FStateTreeCompareDistanceCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCompareDistanceConditionInstanceData InstanceDataType;

	FStateTreeCompareDistanceCondition() = default;
	explicit FStateTreeCompareDistanceCondition(const EGenericAICheck InOperator, const EStateTreeCompare InInverts = EStateTreeCompare::Default)
		: bInvert(InInverts == EStateTreeCompare::Invert)
		, Operator(InOperator)
	{}
	
	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCompareDistanceConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;

	UPROPERTY(EditAnywhere, Category = Condition)
	EGenericAICheck Operator = EGenericAICheck::Equal;
};

/**
* Random condition
*/

USTRUCT()
struct STATETREEMODULE_API FStateTreeRandomConditionInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Threshold = 0.5f;
};
STATETREE_POD_INSTANCEDATA(FStateTreeRandomConditionInstanceData);

USTRUCT(DisplayName = "Random")
struct STATETREEMODULE_API FStateTreeRandomCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeRandomConditionInstanceData InstanceDataType;

	FStateTreeRandomCondition() = default;
	
	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeRandomConditionInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
