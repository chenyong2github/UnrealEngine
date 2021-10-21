// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/StateTreeTask_Test.h"


FStateTreeTask2_Test::FStateTreeTask2_Test()
{
}

FStateTreeTask2_Test::~FStateTreeTask2_Test()
{
}

EStateTreeRunStatus FStateTreeTask2_Test::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	// Keeping this here until we get Gameplay Debugger to work again.
//	GEngine->AddOnScreenDebugMessage(INDEX_NONE, 1.0f, FColor::Blue, *FString::Printf(TEXT("[%s] Value=%f IntVal=%d bBoolVal=%s\n"), *Name.ToString(), Value, IntVal, *LexToString(bBoolVal)));

	return EStateTreeRunStatus::Running;
}

#if WITH_GAMEPLAY_DEBUGGER
void FStateTreeTask2_Test::AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const
{
	DebugString += FString::Printf(TEXT("[%s] Value=%f IntVal=%d bBoolVal=%s\n"), *Name.ToString(), Value, IntVal, *LexToString(bBoolVal));
}
#endif

