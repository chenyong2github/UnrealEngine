// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/StateTreeTask_Test.h"
#include "Engine/Engine.h"


UStateTreeTask_Test::UStateTreeTask_Test(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
}

bool UStateTreeTask_Test::Initialize(FStateTreeInstance& StateTreeInstance)
{
	return true;
}

void UStateTreeTask_Test::Activate(FStateTreeInstance& StateTreeInstance)
{
	Time = 0.0f;
}

void UStateTreeTask_Test::Deactivate(FStateTreeInstance& StateTreeInstance)
{
}

EStateTreeRunStatus UStateTreeTask_Test::Tick(FStateTreeInstance& StateTreeInstance, const float DeltaTime)
{
	Time += DeltaTime;
	if (Time > Duration)
	{
		return bFail ? EStateTreeRunStatus::Failed : EStateTreeRunStatus::Succeeded;		
	}
	return EStateTreeRunStatus::Running;
}

#if WITH_GAMEPLAY_DEBUGGER
void UStateTreeTask_Test::AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const
{
	Super::AppendDebugInfoString(DebugString, StateTreeInstance);
	DebugString += FString::Printf(TEXT("Time= %f/%f\n"), Time, Duration);
}
#endif

#if WITH_EDITOR
bool UStateTreeTask_Test::ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants, UObject* Outer)
{
	return true;
}
#endif



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

