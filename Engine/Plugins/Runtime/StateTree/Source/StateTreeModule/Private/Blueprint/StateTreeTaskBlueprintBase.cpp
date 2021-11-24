// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "CoreMinimal.h"
#include "StateTreeExecutionContext.h"

//----------------------------------------------------------------------//
//  UStateTreeTaskBlueprintBase
//----------------------------------------------------------------------//

EStateTreeRunStatus UStateTreeTaskBlueprintBase::ReceiveEnterState_Implementation(AActor* OwnerActor, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus UStateTreeTaskBlueprintBase::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	return ReceiveEnterState(OwnerActor, ChangeType, Transition);
}

void UStateTreeTaskBlueprintBase::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	ReceiveExitState(OwnerActor, ChangeType, Transition);
}

void UStateTreeTaskBlueprintBase::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	ReceiveStateCompleted(OwnerActor, CompletionStatus, CompletedState);
}

EStateTreeRunStatus UStateTreeTaskBlueprintBase::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	AActor* OwnerActor = GetOwnerActor(Context);
	return ReceiveTick(OwnerActor, DeltaTime);
}

//----------------------------------------------------------------------//
//  FStateTreeBlueprintTaskWrapper
//----------------------------------------------------------------------//

bool FStateTreeBlueprintTaskWrapper::Link(FStateTreeLinker& Linker)
{
	const UStateTreeTaskBlueprintBase* TaskCDO = TaskClass ? TaskClass->GetDefaultObject<UStateTreeTaskBlueprintBase>() : nullptr;
	if (TaskCDO != nullptr)
	{
		TaskCDO->LinkExternalData(Linker, ExternalDataHandles);
	}
	
	return true;
}

EStateTreeRunStatus FStateTreeBlueprintTaskWrapper::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	UStateTreeTaskBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeTaskBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	return Instance->EnterState(Context, ChangeType, Transition);
}

void FStateTreeBlueprintTaskWrapper::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	UStateTreeTaskBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeTaskBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->ExitState(Context, ChangeType, Transition);
}

void FStateTreeBlueprintTaskWrapper::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const
{
	UStateTreeTaskBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeTaskBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	Instance->StateCompleted(Context, CompletionStatus, CompletedState);
}

EStateTreeRunStatus FStateTreeBlueprintTaskWrapper::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	UStateTreeTaskBlueprintBase* Instance = Context.GetInstanceObjectInternal<UStateTreeTaskBlueprintBase>(DataViewIndex);
	check(Instance);
	
	Instance->CopyExternalData(Context, ExternalDataHandles);
	return Instance->Tick(Context, DeltaTime);
}
