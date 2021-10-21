// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/StateTreeEvaluator_Test.h"
#include "CoreMinimal.h"
#include "Engine/Engine.h"

FStateTreeEvaluator2_Test::FStateTreeEvaluator2_Test()
{
}

FStateTreeEvaluator2_Test::~FStateTreeEvaluator2_Test()
{
}

void FStateTreeEvaluator2_Test::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	Foo = 0.0f;
	IntVal = 0;
	bBoolVal = false;
	OutResult = &DummyResult;
	DummyResult.Count = 0;
}

void FStateTreeEvaluator2_Test::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	OutResult = &DummyResult;
}

void FStateTreeEvaluator2_Test::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime)
{

	Foo += DeltaTime;
	IntVal = int32(Foo / 4.0f);
	bBoolVal = FMath::Frac(Foo / 3.0f) > (2.5f / 3.0f);

	if (FDummyStateTreeResult* OtherDummy = InResult.GetMutablePtr<FDummyStateTreeResult>())
	{
		OtherDummy->Count = IntVal;
	}
	
	OutResult = &DummyResult;

	// Keeping this here until we get Gameplay Debugger to work again.
//	GEngine->AddOnScreenDebugMessage(INDEX_NONE, 1.0f, FColor::Orange, *FString::Printf(TEXT("[%s] Foo=%f IntVal=%d bBoolVal=%s\n"), *Name.ToString(), Foo, IntVal, *LexToString(bBoolVal)));
}
