// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeInstance.h"
#include "StateTree.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeVariableDesc.h"
#include "StateTreeParameterStorage.h"
#include "StateTreeParameterLayout.h"
#include "StateTreeConditionBase.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "GameFramework/Actor.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "VisualLogger/VisualLogger.h"

#define STATETREE_LOG(Verbosity, Format, ...) UE_VLOG(GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)
#define STATETREE_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG((Condition), GetOwner(), LogStateTree, Verbosity, Format, ##__VA_ARGS__)

//PRAGMA_DISABLE_OPTIMIZATION

namespace FStateTreeHelpers
{
	bool CompareBools(const bool bLHS, const bool bRHS, const EGenericAICheck Operator)
	{
		switch (Operator)
		{
		case EGenericAICheck::Equal:
			return bLHS == bRHS;
		case EGenericAICheck::NotEqual:
			return bLHS != bRHS;
		default:
			ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
			break;
		}
		return false;
	}

	bool CompareInts(const int LHS, const int RHS, const EGenericAICheck Operator)
	{
		switch (Operator)
		{
		case EGenericAICheck::Equal:
			return LHS == RHS;
		case EGenericAICheck::NotEqual:
			return LHS != RHS;
		case EGenericAICheck::Less:
			return LHS < RHS;
		case EGenericAICheck::LessOrEqual:
			return LHS <= RHS;
		case EGenericAICheck::Greater:
			return LHS > RHS;
		case EGenericAICheck::GreaterOrEqual:
			return LHS >= RHS;
		default:
			ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
			break;
		}
		return false;
	}

	bool CompareFloats(const float LHS, const float RHS, const EGenericAICheck Operator)
	{
		switch (Operator)
		{
		case EGenericAICheck::Equal:
			return LHS == RHS;
		case EGenericAICheck::NotEqual:
			return LHS != RHS;
		case EGenericAICheck::Less:
			return LHS < RHS;
		case EGenericAICheck::LessOrEqual:
			return LHS <= RHS;
		case EGenericAICheck::Greater:
			return LHS > RHS;
		case EGenericAICheck::GreaterOrEqual:
			return LHS >= RHS;
		default:
			ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
			break;
		}
		return false;
	}

	bool CompareVectors(const FVector& LHS, const FVector& RHS, const EGenericAICheck Operator)
	{
		switch (Operator)
		{
		case EGenericAICheck::Equal:
			return LHS.Equals(RHS);
		case EGenericAICheck::NotEqual:
			return !LHS.Equals(RHS);
		default:
			ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
			break;
		}
		return false;
	}

	bool CompareObjects(const FWeakObjectPtr LHS, const FWeakObjectPtr RHS, const EGenericAICheck Operator)
	{
		switch (Operator)
		{
		case EGenericAICheck::Equal:
			return LHS == RHS;
		case EGenericAICheck::NotEqual:
			return LHS != RHS;
		default:
			ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
			break;
		}
		return false;
	}
};



FStateTreeInstance::FStateTreeInstance()
	: Owner(nullptr)
	, StateTree(nullptr)
	, CurrentState(FStateTreeHandle::Invalid)
	, LastTickStatus(EStateTreeRunStatus::Failed)
	, CurrentTask(FStateTreeHandle::Invalid)
	, NextTask(FStateTreeHandle::Invalid)
	, RunStatus(EStateTreeRunStatus::Failed)
{
}

FStateTreeInstance::~FStateTreeInstance()
{
}

void FStateTreeInstance::Reset()
{
	Owner = nullptr;
	StateTree = nullptr;
	Evaluators.Reset();
	VariableMemoryPool.Reset();
	CurrentState = FStateTreeHandle::Invalid;
	CurrentTask = FStateTreeHandle::Invalid;
	NextTask = FStateTreeHandle::Invalid;
	RunStatus = EStateTreeRunStatus::Failed;
}

bool FStateTreeInstance::Init(UObject& InOwner, const UStateTree& InStateTree)
{
	if (!InStateTree.IsValidStateTree())
	{
		STATETREE_LOG(Error, TEXT("%s: StateTree asset '%' is not valid."), ANSI_TO_TCHAR(__FUNCTION__), *InStateTree.GetName());
		Reset();
		return false;
	}

	if (InStateTree.IsV2())
	{
		STATETREE_LOG(Error, TEXT("%s: V2 Assets are not supported."), ANSI_TO_TCHAR(__FUNCTION__), *InStateTree.GetName());
		Reset();
		return false;
	}

	Owner = &InOwner;
	StateTree = &InStateTree;

	CurrentState = FStateTreeHandle::Invalid;
	CurrentTask = FStateTreeHandle::Invalid;
	NextTask = FStateTreeHandle::Invalid;
	RunStatus = EStateTreeRunStatus::Failed;

	// Initialize variable memory
	VariableMemoryPool.SetNumZeroed(int32(StateTree->Variables.GetMemoryUsage()));

	// Instantiate Evaluators
	for (const UStateTreeEvaluatorBase* Eval : StateTree->Evaluators)
	{
		UStateTreeEvaluatorBase* EvalInstance = Cast<UStateTreeEvaluatorBase>(StaticDuplicateObject(Eval, Owner));
		if (!EvalInstance)
		{
			STATETREE_LOG(Error, TEXT("%s: Failed to create StateTree '%' Evaluator '%s' on '%s'."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *GetNameSafe(Eval), *GetNameSafe(Owner));
			Reset();
			return false;
		}
		if (!EvalInstance->Initialize(*this))
		{
			STATETREE_LOG(Error, TEXT("%s: Failed to initialize StateTree '%' Evaluator '%s' on '%s'."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *GetNameSafe(Eval), *GetNameSafe(Owner));
			Reset();
			return false;
		}
		Evaluators.Add(EvalInstance);
	}

	// Instantiate Tasks
	for (const UStateTreeTaskBase* Task : StateTree->Tasks)
	{
		// TODO: move evaluators into structs too so that we can skip the owner here.
		UStateTreeTaskBase* TaskInstance = Cast<UStateTreeTaskBase>(StaticDuplicateObject(Task, Owner));
		if (!TaskInstance)
		{
			STATETREE_LOG(Error, TEXT("%s: Failed to create StateTree '%' Task '%s' on '%s'."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *GetNameSafe(Task), *GetNameSafe(Owner));
			Reset();
			return false;
		}
		if (!TaskInstance->Initialize(*this))
		{
			STATETREE_LOG(Error, TEXT("%s: Failed to initialize StateTree '%' Task '%s' on '%s'."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(StateTree), *GetNameSafe(Task), *GetNameSafe(Owner));
			Reset();
			return false;
		}
		Tasks.Add(TaskInstance);
	}

	return true;
}

bool FStateTreeInstance::TestCondition(const FStateTreeCondition& Cond)
{
	switch (Cond.Left.Type)
	{
	case EStateTreeVariableType::Bool:
		return FStateTreeHelpers::CompareBools(GetValueBool(Cond.Left.Handle), GetValueBool(Cond.Right.Handle), Cond.Operator);
	case EStateTreeVariableType::Int:
		return FStateTreeHelpers::CompareInts(GetValueInt(Cond.Left.Handle), GetValueInt(Cond.Right.Handle), Cond.Operator);
	case EStateTreeVariableType::Float:
		return FStateTreeHelpers::CompareFloats(GetValueFloat(Cond.Left.Handle), GetValueFloat(Cond.Right.Handle), Cond.Operator);
	case EStateTreeVariableType::Vector:
		return FStateTreeHelpers::CompareVectors(GetValueVector(Cond.Left.Handle), GetValueVector(Cond.Right.Handle), Cond.Operator);
	case EStateTreeVariableType::Object:
		return FStateTreeHelpers::CompareObjects(GetValueObject(Cond.Left.Handle), GetValueObject(Cond.Right.Handle), Cond.Operator);
	default:
		ensureMsgf(false, TEXT("Unhandled type %d"), Cond.Left.Type);
		return false;
	}

	return false;
}

bool FStateTreeInstance::TestAllConditions(const uint32 ConditionsOffset, const uint32 ConditionsNum)
{
	for (uint32 i = 0; i < ConditionsNum; i++)
	{
		if (!TestCondition(StateTree->Conditions[ConditionsOffset + i]))
		{
			return false;
		}
	}
	return true;
}

bool FStateTreeInstance::GetValueBool(const FStateTreeHandle Handle, const bool DefaultValue) const
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::GetValueFromMemory<bool>(&VariableMemoryPool[Handle.Index]);
	}
	return StateTree ? StateTree->Constants.GetConstantBool(Handle, DefaultValue) : DefaultValue;
}

int32 FStateTreeInstance::GetValueInt(const FStateTreeHandle Handle, const int32 DefaultValue) const
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::GetValueFromMemory<int32>(&VariableMemoryPool[Handle.Index]);
	}
	return StateTree ? StateTree->Constants.GetConstantInt(Handle, DefaultValue) : DefaultValue;
}

float FStateTreeInstance::GetValueFloat(const FStateTreeHandle Handle, const float DefaultValue) const
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::GetValueFromMemory<float>(&VariableMemoryPool[Handle.Index]);
	}
	return StateTree ? StateTree->Constants.GetConstantFloat(Handle, DefaultValue) : DefaultValue;
}

const FVector& FStateTreeInstance::GetValueVector(const FStateTreeHandle Handle, const FVector& DefaultValue) const
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::GetValueFromMemory<FVector>(&VariableMemoryPool[Handle.Index]);
	}
	return StateTree ? StateTree->Constants.GetConstantVector(Handle, DefaultValue) : DefaultValue;
}

const FWeakObjectPtr& FStateTreeInstance::GetValueObject(const FStateTreeHandle Handle, const FWeakObjectPtr& DefaultValue) const
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::GetValueFromMemory<FWeakObjectPtr>(&VariableMemoryPool[Handle.Index]);
	}
	return DefaultValue;
}

bool FStateTreeInstance::GetValue(const FStateTreeHandle Handle, EStateTreeVariableType Type, uint8* Value) const
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		FMemory::Memcpy(Value, &VariableMemoryPool[Handle.Index], FStateTreeVariableHelpers::GetVariableMemoryUsage(Type));
		return true;
	}
	return StateTree ? StateTree->Constants.GetConstant(Handle, Type, Value) : false;
}


bool FStateTreeInstance::SetValueBool(const FStateTreeHandle Handle, const bool Value)
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::SetValueInMemory<bool>(&VariableMemoryPool[Handle.Index], Value);
	}
	return false;
}

bool FStateTreeInstance::SetValueInt(const FStateTreeHandle Handle, const int32 Value)
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::SetValueInMemory<int32>(&VariableMemoryPool[Handle.Index], Value);
	}
	return false;
}

bool FStateTreeInstance::SetValueFloat(const FStateTreeHandle Handle, const float Value)
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::SetValueInMemory<float>(&VariableMemoryPool[Handle.Index], Value);
	}
	return false;
}

bool FStateTreeInstance::SetValueVector(const FStateTreeHandle Handle, const FVector& Value)
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::SetValueInMemory<FVector>(&VariableMemoryPool[Handle.Index], Value);
	}
	return false;
}

bool FStateTreeInstance::SetValueObject(const FStateTreeHandle Handle, FWeakObjectPtr Value)
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		return FStateTreeVariableHelpers::SetWeakObjectInMemory(&VariableMemoryPool[Handle.Index], Value);
	}
	return false;
}

bool FStateTreeInstance::SetValue(const FStateTreeHandle Handle, EStateTreeVariableType Type, const uint8* Value)
{
	if (Handle.Index < uint16(VariableMemoryPool.Num())) // Handles invalid handles too, Invalid handle is largest possible value.
	{
		FMemory::Memcpy(&VariableMemoryPool[Handle.Index], Value, FStateTreeVariableHelpers::GetVariableMemoryUsage(Type));
		return true;
	}
	return false;
}

bool FStateTreeInstance::ReadParametersFromStorage(const FStateTreeParameterLayout& ParameterLayout, const FStateTreeParameterStorage& ParameterStorage)
{
	// Expect reader to know the layout
	if (uint32(ParameterLayout.Parameters.Num()) != ParameterStorage.GetParameterNum())
	{
		return false;
	}

	for (int32 i = 0; i < ParameterLayout.Parameters.Num(); i++)
	{
		const FStateTreeParameter& Param = ParameterLayout.Parameters[i];
		const uint8* ValuePtr = ParameterStorage.GetParameterPtr(uint32(i), Param.Variable.Type);
		if (!ValuePtr)
		{
			return false;
		}
		SetValue(Param.Variable.Handle, Param.Variable.Type, ValuePtr);
	}
	return true;
}

bool FStateTreeInstance::WriteParametersToStorage(const FStateTreeParameterLayout& ParameterLayout, FStateTreeParameterStorage& ParameterStorage) const
{
	// Expect writer to know the layout
	if (uint32(ParameterLayout.Parameters.Num()) != ParameterStorage.GetParameterNum())
	{
		return false;
	}

	for (int32 i = 0; i < ParameterLayout.Parameters.Num(); i++)
	{
		const FStateTreeParameter& Param = ParameterLayout.Parameters[i];
		uint8* ValuePtr = ParameterStorage.GetParameterPtr(uint32(i), Param.Variable.Type);
		if (!ValuePtr)
		{
			return false;
		}
		GetValue(Param.Variable.Handle, Param.Variable.Type, ValuePtr);
	}
	return true;
}

void FStateTreeInstance::ResetState()
{
	if (CurrentState.IsValid())
	{
		ExitState(CurrentState);
		CurrentTask = FStateTreeHandle::Invalid;
		NextTask = FStateTreeHandle::Invalid;
	}

	CurrentState = FStateTreeHandle::Invalid;
}

void FStateTreeInstance::ActivateEvaluators()
{
	for (UStateTreeEvaluatorBase* Eval : Evaluators)
	{
		Eval->Activate(*this);
	}
}

void FStateTreeInstance::DeactivateEvaluators()
{
	for (UStateTreeEvaluatorBase* Eval : Evaluators)
	{
		Eval->Deactivate(*this);
	}
}

void FStateTreeInstance::TickEvaluators(const float DeltaTime)
{
	for (UStateTreeEvaluatorBase* Eval : Evaluators)
	{
		Eval->Tick(*this, DeltaTime);
	}
}


void FStateTreeInstance::Start()
{
	if (!Owner || !StateTree)
	{
		return;
	}

	ActivateEvaluators();
	ResetState();
	RunStatus = EStateTreeRunStatus::Running;
}

void FStateTreeInstance::Stop()
{
	if (!Owner || !StateTree)
	{
		return;
	}

	DeactivateEvaluators();
	ResetState();
	if (RunStatus == EStateTreeRunStatus::Running)
	{
		RunStatus = EStateTreeRunStatus::Succeeded;
	}
}

void FStateTreeInstance::EnterState(const FStateTreeHandle StateHandle)
{
	if (!StateHandle.IsValid())
	{
		return;
	}
	const FBakedStateTreeState& State = StateTree->States[StateHandle.Index];

	if (State.TasksNum == 0)
	{
		STATETREE_LOG(Error, TEXT("%s: Trying to enter State '%s' without Tasks on '%s' using StateTree %s."), ANSI_TO_TCHAR(__FUNCTION__), *State.Name.ToString(), *GetNameSafe(Owner), *GetNameSafe(StateTree));
	}

	CurrentTask = FStateTreeHandle::Invalid;
	NextTask = FStateTreeHandle::Invalid;
}

void FStateTreeInstance::ExitState(const FStateTreeHandle StateHandle)
{
	if (!StateHandle.IsValid())
	{
		return;
	}
	const FBakedStateTreeState& State = StateTree->States[StateHandle.Index];

	// Stop any tasks in progress.
	if (CurrentTask.IsValid())
	{
		Tasks[State.TasksBegin + CurrentTask.Index]->Deactivate(*this);
	}
}

EStateTreeRunStatus FStateTreeInstance::TickState(const FStateTreeHandle StateHandle, const float DeltaTime)
{
	if (!StateHandle.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}
	const FBakedStateTreeState& State = StateTree->States[StateHandle.Index];
	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;

	// Invalid NextTasks means restart of the sequence (i.e. enter state or restart sequence)
	if (!NextTask.IsValid())
	{
		// Stop current task if running.
		if (CurrentTask.IsValid())
		{
			Tasks[State.TasksBegin + CurrentTask.Index]->Deactivate(*this);
			CurrentTask = FStateTreeHandle::Invalid;
		}
		// Start from first task
		NextTask = FStateTreeHandle(0);
	}

	// Change task if requested
	if (CurrentTask.Index != NextTask.Index)
	{
		if (CurrentTask.IsValid())
		{
			Tasks[State.TasksBegin + CurrentTask.Index]->Deactivate(*this);
		}

		CurrentTask = NextTask;

		Tasks[State.TasksBegin + CurrentTask.Index]->Activate(*this);
	}

	if (CurrentTask.IsValid())
	{
		Result = Tasks[State.TasksBegin + CurrentTask.Index]->Tick(*this, DeltaTime);
	}

	// If task succeeded, goto next task or report that the sequence has succeeded.
	if (Result == EStateTreeRunStatus::Succeeded)
	{
		// Goto next Task
		NextTask.Index = CurrentTask.Index + 1;
		if (NextTask.Index < State.TasksNum)
		{
			Result = EStateTreeRunStatus::Running;
		}
		else
		{
			// Mark to restart sequence in case the same State gets selected again.
			NextTask = FStateTreeHandle::Invalid;
			Result = EStateTreeRunStatus::Succeeded;
		}
	}

	return Result;
}

FStateTreeInstance::FStateSelectionResult FStateTreeInstance::TriggerTransitions(const FStateTreeHandle CurrentStateHandle)
{
	// Walk towards root and check all transitions along the way.
	for (FStateTreeHandle Handle = CurrentStateHandle; Handle.IsValid(); Handle = StateTree->States[Handle.Index].Parent)
	{
		const FBakedStateTreeState& State = StateTree->States[Handle.Index];
		for (uint32 i = 0; i < State.TransitionsNum; i++)
		{
			// All transition conditions must pass
			const FBakedStateTransition& Transition = StateTree->Transitions[State.TransitionsBegin + i];
			if (TestAllConditions(Transition.ConditionsBegin, Transition.ConditionsNum))
			{
				if (Transition.Type == EStateTreeTransitionType::GotoState || Transition.Type == EStateTreeTransitionType::NextState)
				{
					FStateSelectionResult SelectedState = SelectState(Transition.State);
					if (SelectedState.IsValid())
					{
						return SelectedState;
					}
				}
				else if (Transition.Type == EStateTreeTransitionType::SelectChildState)
				{
					FStateSelectionResult SelectedState = SelectChildState(CurrentStateHandle);
					if (SelectedState.IsValid())
					{
						return SelectedState;
					}
				}
				else if (Transition.Type == EStateTreeTransitionType::NotSet)
				{
					// NotSet is no-operation, but can be used to mask a transition at parent state. Returning invalid keeps updating current state.
					return FStateSelectionResult(FStateTreeHandle::Invalid);
				}
				else if (Transition.Type == EStateTreeTransitionType::Succeeded)
				{
					return FStateSelectionResult(EStateTreeRunStatus::Succeeded);
				}
				else
				{
					return FStateSelectionResult(EStateTreeRunStatus::Failed);
				}
			}
		}
	}

	// No transition triggered, keep on updating current state.
	return FStateSelectionResult(FStateTreeHandle::Invalid);
}

FStateTreeInstance::FStateSelectionResult FStateTreeInstance::TriggerDefaultTransition(const FStateTreeHandle StateHandle, EStateTreeRunStatus StateTickStatus)
{
	const FBakedStateTreeState& State = StateTree->States[StateHandle.Index];

	EStateTreeTransitionType TransitionType = State.StateDoneTransitionType;
	FStateTreeHandle TransitionState = State.StateDoneTransitionState;
	
	// Use State Failed transition if state has failed, and failed transition is set.
	if (StateTickStatus == EStateTreeRunStatus::Failed && State.StateFailedTransitionType != EStateTreeTransitionType::NotSet)
	{
		TransitionType = State.StateFailedTransitionType;
		TransitionState = State.StateFailedTransitionState;
	}
	
	if ((TransitionType == EStateTreeTransitionType::GotoState || TransitionType == EStateTreeTransitionType::NextState) && TransitionState.IsValid())
	{
		// If default transition is set to specific state, run selection logic from there.
		FStateTreeInstance::FStateSelectionResult Result = SelectState(TransitionState);
		if (Result.IsValid())
		{
			return Result;
		}
		else
		{
			STATETREE_LOG(Error, TEXT("%s: Default transition on state '%s' could not be triggered. '%s' using StateTree %s."), ANSI_TO_TCHAR(__FUNCTION__), *State.Name.ToString(), *GetNameSafe(Owner), *GetNameSafe(StateTree));
			return FStateSelectionResult(EStateTreeRunStatus::Failed);
		}
	}
	else if (TransitionType == EStateTreeTransitionType::SelectChildState)
	{
		// If no default transition specified, run selection logic on children.
		FStateTreeInstance::FStateSelectionResult Result = SelectChildState(StateHandle);
		if (Result.IsValid())
		{
			return Result;
		}
		else
		{
			STATETREE_LOG(Error, TEXT("%s: Default transition on state '%s' could not be triggered. '%s' using StateTree %s."), ANSI_TO_TCHAR(__FUNCTION__), *State.Name.ToString(), *GetNameSafe(Owner), *GetNameSafe(StateTree));
			return FStateSelectionResult(EStateTreeRunStatus::Failed);
		}
	}
	else if (TransitionType == EStateTreeTransitionType::Succeeded)
	{
		return FStateSelectionResult(EStateTreeRunStatus::Succeeded);
	}

	return FStateSelectionResult(EStateTreeRunStatus::Failed);
}

FStateTreeInstance::FStateSelectionResult FStateTreeInstance::SelectState(const FStateTreeHandle StateHandle)
{
	const FBakedStateTreeState& State = StateTree->States[StateHandle.Index];

	// Check that the state can be entered
	if (!TestAllConditions(State.EnterConditionsBegin, State.EnterConditionsNum))
	{
		return FStateSelectionResult(FStateTreeHandle::Invalid);
	}

	// If the state has any tasks, select it.
	if (State.TasksNum > 0)
	{
		return FStateSelectionResult(StateHandle);
	}
	// Else proceed to select any of the children.
	return SelectChildState(StateHandle);
}

FStateTreeInstance::FStateSelectionResult FStateTreeInstance::SelectChildState(const FStateTreeHandle StateHandle)
{
	const FBakedStateTreeState& State = StateTree->States[StateHandle.Index];

	// Visit children in order.
	for (uint16 ChildState = State.ChildrenBegin; ChildState < State.ChildrenEnd;)
	{
		FStateSelectionResult SelectedState = SelectState(FStateTreeHandle(ChildState));
		if (SelectedState.IsValid())
		{
			return SelectedState;
		}
		ChildState = StateTree->States[ChildState].GetNextSibling();
	}

	return FStateSelectionResult(FStateTreeHandle::Invalid);
}

EStateTreeRunStatus FStateTreeInstance::Tick(const float DeltaTime, FStateTreeParameterStorage* InputParameters)
{
	if (RunStatus != EStateTreeRunStatus::Running)
	{
		return RunStatus;
	}

	if (!Owner || !StateTree)
	{
		RunStatus = EStateTreeRunStatus::Failed;
		return RunStatus;
	}

	if (InputParameters)
	{
		FStateTreeParameterLayout MappedParameters;
		InputParameters->MapVariables(MappedParameters, StateTree->Parameters);
		ReadParametersFromStorage(MappedParameters, *InputParameters);
	}

	TickEvaluators(DeltaTime);

	FStateSelectionResult NextState(EStateTreeRunStatus::Failed);

	if (CurrentState == FStateTreeHandle::Invalid)
	{
		// No state has been selected yet, select starting from root.
		static const FStateTreeHandle RootState = FStateTreeHandle(0);
		NextState = SelectState(RootState);
	}
	else
	{
		if (LastTickStatus == EStateTreeRunStatus::Running)
		{
			// Check transitions while running, returns "running/invalid" if no transition is triggered.
			NextState = TriggerTransitions(CurrentState);
		}
		else
		{
			// State completed or failed on last tick, check if we can proceed to the default next state, returns "failed" if transition cannot be triggered.
			NextState = TriggerDefaultTransition(CurrentState, LastTickStatus);
		}
	}

	if (NextState.Status != EStateTreeRunStatus::Running)
	{
		// Transition to a terminal state (succeeded/failed), or default transition failed.
		ExitState(CurrentState);
		RunStatus = NextState.Status;
		return RunStatus;
	}
	if (NextState.IsValid())
	{
		// New state has become active, switch state.
		ExitState(CurrentState);
		CurrentState = NextState.State;
		EnterState(CurrentState);
	}
	if (!CurrentState.IsValid())
	{
		// Should not happen.
		STATETREE_LOG(Error, TEXT("%s: Failed to select state on '%s' using StateTree %s."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(Owner), *GetNameSafe(StateTree));
		RunStatus = EStateTreeRunStatus::Failed;
		return RunStatus;
	}

	// Run state
	LastTickStatus = TickState(CurrentState, DeltaTime);

	return RunStatus;
}

#if ENABLE_VISUAL_LOG
void FStateTreeInstance::DescribeSelfToVisLog(struct FVisualLogEntry& Snapshot) const
{
}
#endif // ENABLE_VISUAL_LOG

#if WITH_GAMEPLAY_DEBUGGER
FString FStateTreeInstance::GetDebugInfoString() const
{
	if (!StateTree)
	{
		return FString(TEXT("No StateTree asset."));
	}

	FString DebugString = FString::Printf(TEXT("StateTree (asset: %s)\n"), *GetNameSafe(StateTree));

	DebugString += TEXT("Status: ");
	switch (RunStatus)
	{
	case EStateTreeRunStatus::Failed:
		DebugString += TEXT("Failed\n");
		break;
	case EStateTreeRunStatus::Succeeded:
		DebugString += TEXT("Succeeded\n");
		break;
	case EStateTreeRunStatus::Running:
		DebugString += TEXT("Running\n");
		break;
	default:
		DebugString += TEXT("--\n");
	}

	// Active States
	TArray<FStateTreeHandle> ActiveStates;
	for (FStateTreeHandle Handle = CurrentState; Handle.IsValid(); Handle = StateTree->States[Handle.Index].Parent)
	{
		ActiveStates.Insert(Handle, 0);
	}

	DebugString += TEXT("Current State:\n");
	for (FStateTreeHandle Handle : ActiveStates)
	{
		if (Handle.IsValid())
		{
			const FBakedStateTreeState& State = StateTree->States[Handle.Index];
			DebugString += FString::Printf(TEXT("[%s] "), *State.Name.ToString());
			if (!StateTree->IsV2())
			{
				if (Handle == CurrentState)
				{
					for (uint32 i = 0; i < State.TasksNum; i++)
					{
						DebugString += FString::Printf(TEXT(" [%s%s] "), i == (uint32)CurrentTask.Index ? TEXT(">>") : TEXT(""), *Tasks[State.TasksBegin + i]->Name.ToString());
					}
				}
			}
			DebugString += TEXT("\n");
		}
	}

	if (CurrentState.IsValid() && CurrentTask.IsValid())
	{
		const FBakedStateTreeState& State = StateTree->States[CurrentState.Index];
		DebugString += FString::Printf(TEXT("\nCurrent Task %s (%d):\n"), *Tasks[State.TasksBegin + CurrentTask.Index]->Name.ToString(), CurrentTask.Index);
		Tasks[State.TasksBegin + CurrentTask.Index]->AppendDebugInfoString(DebugString, *this);
	}

	DebugString += TEXT("\nEvaluators:\n");
	for (const UStateTreeEvaluatorBase* Eval : Evaluators)
	{
		Eval->AppendDebugInfoString(DebugString, *this);
	}

	DebugString += TEXT("\nVariables:\n");
	for (const FStateTreeVariableDesc& Var : StateTree->Variables.Variables)
	{
		const FStateTreeHandle Handle(Var.Offset);
		switch (Var.Type)
		{
		case EStateTreeVariableType::Bool:
			DebugString += FString::Printf(TEXT("    %s = %s\n"), *Var.Name.ToString(), GetValueBool(Handle) ? TEXT("true") : TEXT("false"));
			break;
		case EStateTreeVariableType::Float:
			DebugString += FString::Printf(TEXT("    %s = %f\n"), *Var.Name.ToString(), GetValueFloat(Handle));
			break;
		case EStateTreeVariableType::Int:
			DebugString += FString::Printf(TEXT("    %s = %d\n"), *Var.Name.ToString(), GetValueInt(Handle));
			break;
		case EStateTreeVariableType::Object:
			DebugString += FString::Printf(TEXT("    %s = %s\n"), *Var.Name.ToString(), *GetNameSafe(GetValueObject(Handle).Get()));
			break;
		case EStateTreeVariableType::Vector:
			DebugString += FString::Printf(TEXT("    %s = %s\n"), *Var.Name.ToString(), *GetValueVector(Handle).ToString());
			break;
		default:
			DebugString += FString::Printf(TEXT("    %s = ??\n"), *Var.Name.ToString());
			break;
		}
	}

	return DebugString;
}

#endif // WITH_GAMEPLAY_DEBUGGER

#undef STATETREE_LOG
#undef STATETREE_CLOG
