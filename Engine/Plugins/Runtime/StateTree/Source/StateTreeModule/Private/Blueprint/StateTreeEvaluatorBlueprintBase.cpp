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
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ReceiveEnterState(OwnerActor, ChangeType, Transition);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UStateTreeEvaluatorBlueprintBase::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ReceiveExitState(OwnerActor, ChangeType, Transition);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UStateTreeEvaluatorBlueprintBase::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeActiveStates& CompletedActiveStates)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ReceiveStateCompleted(OwnerActor, CompletionStatus, CompletedActiveStates);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UStateTreeEvaluatorBlueprintBase::TreeStart(FStateTreeExecutionContext& Context)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	ReceiveTreeStart(OwnerActor);
}

void UStateTreeEvaluatorBlueprintBase::TreeStop(FStateTreeExecutionContext& Context)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	ReceiveTreeStop(OwnerActor);
}

void UStateTreeEvaluatorBlueprintBase::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	ReceiveEvaluate(OwnerActor, EStateTreeEvaluationType::Tick, DeltaTime);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	ReceiveTick(OwnerActor, DeltaTime);
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
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Instance->EnterState(Context, ChangeType, Transition);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FStateTreeBlueprintEvaluatorWrapper::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Instance->ExitState(Context, ChangeType, Transition);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FStateTreeBlueprintEvaluatorWrapper::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeActiveStates& CompletedActiveStates) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Instance->StateCompleted(Context, CompletionStatus, CompletedActiveStates);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FStateTreeBlueprintEvaluatorWrapper::TreeStart(FStateTreeExecutionContext& Context) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->TreeStart(Context);
}

void FStateTreeBlueprintEvaluatorWrapper::TreeStop(FStateTreeExecutionContext& Context) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->TreeStop(Context);
}

void FStateTreeBlueprintEvaluatorWrapper::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	UStateTreeEvaluatorBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeEvaluatorBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->Tick(Context, DeltaTime);
}
