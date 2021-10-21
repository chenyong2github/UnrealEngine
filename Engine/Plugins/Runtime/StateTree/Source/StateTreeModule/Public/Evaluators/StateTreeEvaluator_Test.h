// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeEvaluator_Test.generated.h"

UENUM()
namespace EStateTreeOldEnumTest
{
	enum Type
	{
		Foo,
		Bar
	};
}

USTRUCT()
struct FDummyStateTreeResult : public FStateTreeResult
{
	GENERATED_BODY()

	virtual ~FDummyStateTreeResult() {}
	virtual const UScriptStruct& GetStruct() const override { return *StaticStruct(); }

	UPROPERTY(EditAnywhere, Category = Value)
	FName Name;

	UPROPERTY(EditAnywhere, Category = Value)
	int32 Count = 0;
};

USTRUCT(meta=(DisplayName="Evaluator Test"))
struct STATETREEMODULE_API FStateTreeEvaluator_Test : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	FStateTreeEvaluator_Test();
	virtual ~FStateTreeEvaluator_Test();
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) override;

protected:
	UPROPERTY(EditAnywhere, Category = Test, meta = (Bindable))
	float Foo = 0.0f;

	UPROPERTY(EditAnywhere, Category = Test)
	int32 IntVal = 0;

	UPROPERTY(EditAnywhere, Category = Test)
	bool bBoolVal = false;

	UPROPERTY(EditAnywhere, Category = Test)
	FVector VecVal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Test, Meta = (Bindable))
	EStateTreeVariableType EnumVal = EStateTreeVariableType::Void;

	UPROPERTY(EditAnywhere, Category = Test, Meta = (Bindable))
	TEnumAsByte<enum EStateTreeOldEnumTest::Type> OldEnumVal = EStateTreeOldEnumTest::Foo;

	UPROPERTY(EditAnywhere, Category = Test)
	FStateTreeResultRef OutResult;

	UPROPERTY(EditAnywhere, Category = Test, Meta = (Bindable))
	FStateTreeResultRef InResult;
	
	FDummyStateTreeResult DummyResult;
};
