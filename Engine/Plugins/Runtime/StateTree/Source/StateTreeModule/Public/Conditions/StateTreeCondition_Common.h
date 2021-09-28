// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "StateTreeConditionBase.h"
#include "StateTreeAnyEnum.h"
#include "StateTreeCondition_Common.generated.h"

/**
 * Condition comparing two integers.
 */
USTRUCT(DisplayName="Integer Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareInt : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FStateTreeCondition_CompareInt() : FStateTreeConditionBase() { }
	FStateTreeCondition_CompareInt(const int32 InLeft, const EGenericAICheck InOperator, const int32 InRight)
		: FStateTreeConditionBase()
		, Left(InLeft)
		, Operator(InOperator)
		, Right(InRight)
	{}

#if WITH_EDITOR
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const override;
#endif
	virtual bool TestCondition() const override;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	int32 Left = 0;

	UPROPERTY(EditAnywhere, Category = Condition)
	EGenericAICheck Operator = EGenericAICheck::Equal;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	int32 Right = 0;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
 * Condition comparing two floats.
 */
USTRUCT(DisplayName = "Float Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareFloat : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FStateTreeCondition_CompareFloat() : FStateTreeConditionBase() {}
	FStateTreeCondition_CompareFloat(const float InLeft, const EGenericAICheck InOperator, const float InRight)
        : FStateTreeConditionBase()
        , Left(InLeft)
        , Operator(InOperator)
        , Right(InRight)
	{}

#if WITH_EDITOR
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const override;
#endif
	virtual bool TestCondition() const override;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	float Left = 0.0f;

	UPROPERTY(EditAnywhere, Category = Condition)
	EGenericAICheck Operator = EGenericAICheck::Equal;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	float Right = 0.0f;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
 * Condition comparing two booleans.
 */
USTRUCT(DisplayName = "Bool Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareBool : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FStateTreeCondition_CompareBool() : FStateTreeConditionBase() {}
	FStateTreeCondition_CompareBool(const bool bInLeft, const bool bInRight)
        : FStateTreeConditionBase()
        , bLeft(bInLeft)
        , bRight(bInRight)
	{}

#if WITH_EDITOR
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const override;
#endif
	virtual bool TestCondition() const override;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	bool bLeft = false;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	bool bRight = false;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
 * Condition comparing two enums.
 */
USTRUCT(DisplayName = "Enum Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareEnum : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FStateTreeCondition_CompareEnum() : FStateTreeConditionBase() {}
	FStateTreeCondition_CompareEnum(const FStateTreeAnyEnum& InLeft, const FStateTreeAnyEnum& InRight)
        : FStateTreeConditionBase()
        , Left(InLeft)
        , Right(InRight)
	{}

#if WITH_EDITOR
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const override;
	virtual void OnBindingChanged(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup) override;
#endif
	virtual bool TestCondition() const override;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FStateTreeAnyEnum Left;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FStateTreeAnyEnum Right;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};

/**
 * Condition comparing distance between two vectors.
 */
USTRUCT(DisplayName = "Distance Compare")
struct STATETREEMODULE_API FStateTreeCondition_CompareDistance : public FStateTreeConditionBase
{
	GENERATED_BODY()

	FStateTreeCondition_CompareDistance() : FStateTreeConditionBase() { }
	FStateTreeCondition_CompareDistance(const FVector& InSource, const FVector& InTarget, const EGenericAICheck InOperator, const float InDistance)
        : FStateTreeConditionBase()
        , Source(InSource)
        , Target(InTarget)
        , Operator(InOperator)
        , Distance(InDistance)
	{}

#if WITH_EDITOR
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const override;
#endif
	virtual bool TestCondition() const override;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FVector Source = FVector(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	FVector Target = FVector(EForceInit::ForceInitToZero);

	UPROPERTY(EditAnywhere, Category = Condition)
	EGenericAICheck Operator = EGenericAICheck::Equal;

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bindable))
	float Distance = 0.0f;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};
