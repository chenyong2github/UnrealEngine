// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "CoreMinimal.h"
#include "StateTreeExecutionContext.h"

//----------------------------------------------------------------------//
//  UStateTreeEvaluatorBlueprintBase
//----------------------------------------------------------------------//

void UStateTreeEvaluatorBlueprintBase::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	ReceiveEnterState(OwnerActor, ChangeType, Transition);
}

void UStateTreeEvaluatorBlueprintBase::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	ReceiveExitState(OwnerActor, ChangeType, Transition);
}

void UStateTreeEvaluatorBlueprintBase::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	ReceiveStateCompleted(OwnerActor, CompletionStatus, CompletedState);
}

void UStateTreeEvaluatorBlueprintBase::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	ReceiveEvaluate(OwnerActor, EvalType, DeltaTime);
}

//----------------------------------------------------------------------//
//  FStateTreeBlueprintEvaluatorWrapper
//----------------------------------------------------------------------//

bool FStateTreeBlueprintEvaluatorWrapper::Link(FStateTreeLinker& Linker)
{
	const UStateTreeEvaluatorBlueprintBase* EvalCDO = EvaluatorClass ? EvaluatorClass->GetDefaultObject<UStateTreeEvaluatorBlueprintBase>() : nullptr;
	if (EvalCDO != nullptr)
	{
		EvalCDO->LinkExternalData(Linker, ExternalDataHandles);
	}

	return true;
}

void FStateTreeBlueprintEvaluatorWrapper::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->EnterState(Context, ChangeType, Transition);
}

void FStateTreeBlueprintEvaluatorWrapper::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->ExitState(Context, ChangeType, Transition);
}

void FStateTreeBlueprintEvaluatorWrapper::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->StateCompleted(Context, CompletionStatus, CompletedState);
}

void FStateTreeBlueprintEvaluatorWrapper::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->Evaluate(Context, EvalType, DeltaTime);
}
