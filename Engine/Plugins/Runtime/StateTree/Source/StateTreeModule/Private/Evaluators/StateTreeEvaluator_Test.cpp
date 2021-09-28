// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/StateTreeEvaluator_Test.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StateTreeInstance.h"
#include "StateTreeVariableLayout.h"
#include "StateTreeConstantStorage.h"
#include "Engine/Engine.h"

UStateTreeEvaluator_Test::UStateTreeEvaluator_Test(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
	, Timer(0.0f)
	, SomeFloat(EStateTreeVariableBindingMode::Typed, EStateTreeVariableType::Float)
	, BoolVar(EStateTreeVariableBindingMode::Definition, FName(TEXT("Bool")), EStateTreeVariableType::Bool)
	, FloatVar(EStateTreeVariableBindingMode::Definition, FName(TEXT("Float")), EStateTreeVariableType::Float)
	, IntVar(EStateTreeVariableBindingMode::Definition, FName(TEXT("Int")), EStateTreeVariableType::Int)
	, VectorVar(EStateTreeVariableBindingMode::Definition, FName(TEXT("Vector")), EStateTreeVariableType::Vector)
	, Object1Var(EStateTreeVariableBindingMode::Definition, FName(TEXT("Object1")), EStateTreeVariableType::Object, AActor::StaticClass())
	, Object2Var(EStateTreeVariableBindingMode::Definition, FName(TEXT("Object2")), EStateTreeVariableType::Object, AActor::StaticClass())
	, ObjectRVar(EStateTreeVariableBindingMode::Definition, FName(TEXT("ObjectR")), EStateTreeVariableType::Object, UStateTreeEvaluatorBase::StaticClass())
{
}


bool UStateTreeEvaluator_Test::Initialize(FStateTreeInstance& StateTreeInstance)
{
	return true;
}

void UStateTreeEvaluator_Test::Activate(FStateTreeInstance& StateTreeInstance)
{
	// Clear variables
	StateTreeInstance.SetValueBool(BoolVar.Handle, false);
	StateTreeInstance.SetValueFloat(FloatVar.Handle, 0.0f);
	StateTreeInstance.SetValueInt(IntVar.Handle, 0);
	StateTreeInstance.SetValueVector(VectorVar.Handle, FVector(1.0f, 20.0f, 300.0f));
	StateTreeInstance.SetValueObject(Object1Var.Handle, nullptr);
	StateTreeInstance.SetValueObject(Object2Var.Handle, nullptr);
	StateTreeInstance.SetValueObject(ObjectRVar.Handle, nullptr);

	Timer = 0.0f;
}

void UStateTreeEvaluator_Test::Deactivate(FStateTreeInstance& StateTreeInstance)
{
}

void UStateTreeEvaluator_Test::Tick(FStateTreeInstance& StateTreeInstance, const float DeltaTime)
{
	Timer += DeltaTime;

	const float FloatVal = StateTreeInstance.GetValueFloat(SomeFloat.Handle, 0.0f);

	StateTreeInstance.SetValueFloat(FloatVar.Handle, Timer);
	StateTreeInstance.SetValueInt(IntVar.Handle, int32(Timer / 4.0f));
	StateTreeInstance.SetValueBool(BoolVar.Handle, FMath::Frac(Timer / 3.0f) > (2.5f / 3.0f));
}

#if WITH_GAMEPLAY_DEBUGGER
void UStateTreeEvaluator_Test::AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const
{
	Super::AppendDebugInfoString(DebugString, StateTreeInstance);
}
#endif // WITH_GAMEPLAY_DEBUGGER


#if WITH_EDITOR
void UStateTreeEvaluator_Test::DefineOutputVariables(FStateTreeVariableLayout& Variables) const
{
	Variables.DefineVariable(Name, BoolVar);
	Variables.DefineVariable(Name, FloatVar);
	Variables.DefineVariable(Name, IntVar);
	Variables.DefineVariable(Name, VectorVar);
	Variables.DefineVariable(Name, Object1Var);
	Variables.DefineVariable(Name, Object2Var);
	Variables.DefineVariable(Name, ObjectRVar);
}

bool UStateTreeEvaluator_Test::ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants, UObject* Outer)
{
	SomeFloat.ResolveHandle(Variables, Constants);

	BoolVar.ResolveHandle(Variables, Constants);
	FloatVar.ResolveHandle(Variables, Constants);
	IntVar.ResolveHandle(Variables, Constants);
	VectorVar.ResolveHandle(Variables, Constants);
	Object1Var.ResolveHandle(Variables, Constants);
	Object2Var.ResolveHandle(Variables, Constants);
	ObjectRVar.ResolveHandle(Variables, Constants);

	return true;
}
#endif


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
