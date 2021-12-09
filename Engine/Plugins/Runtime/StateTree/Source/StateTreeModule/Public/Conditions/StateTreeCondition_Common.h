// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "InstancedStruct.h"
#include "StateTreeConditionBase.h"
#include "StateTreeAnyEnum.h"
#include "StateTreeCondition_Common.generated.h"

enum class EStateTreeCompare : uint8
{
	Default,
	Invert,
};

/**
 * Condition comparing two integers.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCondition_CompareIntInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Input)
	int32 Left = 0;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 Right = 0;
};

USTRUCT(DisplayName="Integer Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareInt : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCondition_CompareIntInstanceData InstanceDataType;

	FStateTreeCondition_CompareInt() = default;
	explicit FStateTreeCondition_CompareInt(const EGenericAICheck InOperator, const EStateTreeCompare InInverts = EStateTreeCompare::Default)
		: bInvert(InInverts == EStateTreeCompare::Invert)
		, Operator(InOperator)
	{}

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCondition_CompareIntInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EGenericAICheck Operator = EGenericAICheck::Equal;
	
	TStateTreeInstanceDataPropertyHandle<int32> LeftHandle;
	TStateTreeInstanceDataPropertyHandle<int32> RightHandle;
};

/**
 * Condition comparing two floats.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCondition_CompareFloatInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	float Left = 0.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Right = 0.0f;
};

USTRUCT(DisplayName = "Float Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareFloat : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCondition_CompareFloatInstanceData InstanceDataType;

	FStateTreeCondition_CompareFloat() = default;
	explicit FStateTreeCondition_CompareFloat(const EGenericAICheck InOperator, const EStateTreeCompare InInverts = EStateTreeCompare::Default)
		: bInvert(InInverts == EStateTreeCompare::Invert)
		, Operator(InOperator)
	{}

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCondition_CompareFloatInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EGenericAICheck Operator = EGenericAICheck::Equal;
	
	TStateTreeInstanceDataPropertyHandle<float> LeftHandle;
	TStateTreeInstanceDataPropertyHandle<float> RightHandle;
};

/**
 * Condition comparing two booleans.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCondition_CompareBoolInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	bool bLeft = false;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bRight = false;
};

USTRUCT(DisplayName = "Bool Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareBool : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCondition_CompareBoolInstanceData InstanceDataType;

	FStateTreeCondition_CompareBool() = default;
	explicit FStateTreeCondition_CompareBool(const EStateTreeCompare InInverts)
		: bInvert(InInverts == EStateTreeCompare::Invert)
	{}

	FStateTreeCondition_CompareBool(const bool bInInverts)
		: bInvert(bInInverts)
	{}

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCondition_CompareBoolInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;

	TStateTreeInstanceDataPropertyHandle<bool> LeftHandle;
	TStateTreeInstanceDataPropertyHandle<bool> RightHandle;
};

/**
 * Condition comparing two enums.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCondition_CompareEnumInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FStateTreeAnyEnum Left;

	UPROPERTY(EditAnywhere, Category = Parameter)
	FStateTreeAnyEnum Right;
};

USTRUCT(DisplayName = "Enum Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareEnum : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCondition_CompareEnumInstanceData InstanceDataType;

	FStateTreeCondition_CompareEnum() = default;
	explicit FStateTreeCondition_CompareEnum(const EStateTreeCompare InInverts)
		: bInvert(InInverts == EStateTreeCompare::Invert)
	{}

	FStateTreeCondition_CompareEnum(const bool bInInverts)
		: bInvert(bInInverts)
	{}

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCondition_CompareEnumInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
	virtual void OnBindingChanged(const FGuid& ID, FStateTreeDataView InstanceData, const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup) override;
#endif

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bInvert = false;

	TStateTreeInstanceDataPropertyHandle<FStateTreeAnyEnum> LeftHandle;
	TStateTreeInstanceDataPropertyHandle<FStateTreeAnyEnum> RightHandle;
};

/**
 * Condition comparing distance between two vectors.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeCondition_CompareDistanceInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FVector Source = FVector(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = Parameter)
	FVector Target = FVector(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = Parameter)
	float Distance = 0.0f;
};

USTRUCT(DisplayName = "Distance Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareDistance : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	typedef FStateTreeCondition_CompareDistanceInstanceData InstanceDataType;

	FStateTreeCondition_CompareDistance() = default;
	explicit FStateTreeCondition_CompareDistance(const EGenericAICheck InOperator, const EStateTreeCompare InInverts = EStateTreeCompare::Default)
		: bInvert(InInverts == EStateTreeCompare::Invert)
		, Operator(InOperator)
	{}
	
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCondition_CompareDistanceInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;

	UPROPERTY(EditAnywhere, Category = Condition)
	EGenericAICheck Operator = EGenericAICheck::Equal;

	TStateTreeInstanceDataPropertyHandle<FVector> SourceHandle;
	TStateTreeInstanceDataPropertyHandle<FVector> TargetHandle;
	TStateTreeInstanceDataPropertyHandle<float> DistanceHandle;
};

/**
* Random condition
*/

USTRUCT()
struct STATETREEMODULE_API FStateTreeCondition_RandomInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Threshold = 0.5f;
};

USTRUCT(DisplayName = "Random")
struct STATETREEMODULE_API FStateTreeCondition_Random : public FStateTreeConditionBase
{
	GENERATED_BODY()

	typedef FStateTreeCondition_RandomInstanceData InstanceDataType;

	FStateTreeCondition_Random() = default;
	
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FStateTreeCondition_RandomInstanceData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceData, const IStateTreeBindingLookup& BindingLookup) const override;
#endif

	TStateTreeInstanceDataPropertyHandle<float> ThresholdHandle;
};
