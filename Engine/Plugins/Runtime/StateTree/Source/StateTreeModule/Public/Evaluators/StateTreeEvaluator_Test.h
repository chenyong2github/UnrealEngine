// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "StateTreeTypes.h"
#include "StateTreeVariable.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeEvaluator_Test.generated.h"

struct FStateTreeVariableLayout;
struct FStateTreeConstantStorage;
class UStateTreeEvaluatorInstanceBase;

/**
 * Test evaluator
 */

UCLASS(BlueprintType, EditInlineNew, CollapseCategories)
class STATETREEMODULE_API UStateTreeEvaluator_Test : public UStateTreeEvaluatorBase
{
	GENERATED_BODY()

public:
	UStateTreeEvaluator_Test(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Called when instantiated first time.
	virtual bool Initialize(FStateTreeInstance& StateTreeInstance) override;
	// Called when evaluator becomes active/ticking.
	virtual void Activate(FStateTreeInstance& StateTreeInstance) override;
	// Called when evaluator becomes inactive.
	virtual void Deactivate(FStateTreeInstance& StateTreeInstance) override;

	// Called on each tick.
	virtual void Tick(FStateTreeInstance& StateTreeInstance, const float DeltaTime) override;

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const override;
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_EDITOR
	virtual void DefineOutputVariables(FStateTreeVariableLayout& Variables) const override;
	virtual bool ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants, UObject* Outer) override;
#endif

protected:
	// State
	float Timer;

	// UI properties
	UPROPERTY(EditDefaultsOnly, Category = Test)
	float PlainNumber;

	UPROPERTY(EditDefaultsOnly, Category = Test)
	FStateTreeVariable SomeFloat;

	// Output variables
	UPROPERTY()
	FStateTreeVariable BoolVar;
	UPROPERTY()
	FStateTreeVariable FloatVar;
	UPROPERTY()
	FStateTreeVariable IntVar;
	UPROPERTY()
	FStateTreeVariable VectorVar;
	UPROPERTY()
	FStateTreeVariable Object1Var;
	UPROPERTY()
	FStateTreeVariable Object2Var;
	UPROPERTY()
	FStateTreeVariable ObjectRVar;
};


// STATETREE_V2

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

USTRUCT(meta=(DisplayName="Evaluator Test2"))
struct STATETREEMODULE_API FStateTreeEvaluator2_Test : public FStateTreeEvaluator2Base
{
	GENERATED_BODY()

	FStateTreeEvaluator2_Test();
	virtual ~FStateTreeEvaluator2_Test();
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

// ~STATETREE_V2
