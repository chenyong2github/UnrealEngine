// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeExecutionContext.h"
#include "StateTree.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConditionBase.h"
#include "Containers/StaticArray.h"
#include "VisualLogger/VisualLogger.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Logging/LogScopedVerbosityOverride.h"

#define STATETREE_LOG(Verbosity, Format, ...) UE_VLOG_UELOG(GetOwner(), LogStateTree, Verbosity, TEXT("%s") Format, *GetInstanceDescription(), ##__VA_ARGS__)
#define STATETREE_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG_UELOG((Condition), GetOwner(), LogStateTree, Verbosity, TEXT("%s") Format, *GetInstanceDescription(), ##__VA_ARGS__)

namespace UE::StateTree
{
	constexpr int32 DebugIndentSize = 2;
}

FStateTreeExecutionContext::FStateTreeExecutionContext()
{
}

FStateTreeExecutionContext::~FStateTreeExecutionContext()
{
}

bool FStateTreeExecutionContext::Init(UObject& InOwner, const UStateTree& InStateTree, const EStateTreeStorage InStorageType)
{
	// Set owner first for proper logging (it will be reset in case of failure) 
	Owner = &InOwner;
	
	if (!InStateTree.IsReadyToRun())
	{
		STATETREE_LOG(Error, TEXT("%s: StateTree asset '%s' is not valid."), ANSI_TO_TCHAR(__FUNCTION__), *InStateTree.GetName());
		Reset();
		return false;
	}
	
	StateTree = &InStateTree;

	StorageType = InStorageType;
	if (StorageType == EStateTreeStorage::Internal)
	{
		// Duplicate instance state.
		InternalInstanceData.CopyFrom(InOwner, StateTree->GetInstanceDataDefaultValue());
	}

	// Initialize data views for all possible items.
	DataViews.SetNum(StateTree->GetNumDataViews());

	// Set data views associated to the parameters using the default values
	SetDefaultParameters();

	// Initialize array to keep track of which states have been updated.
	VisitedStates.Init(false, StateTree->States.Num());

	return true;
}

void FStateTreeExecutionContext::SetDefaultParameters()
{
	if (ensureMsgf(StateTree != nullptr, TEXT("Execution context must be initialized before calling %s"), ANSI_TO_TCHAR(__FUNCTION__))
		&& DataViews.IsValidIndex(StateTree->DefaultParametersDataViewIndex))
	{
		DataViews[StateTree->DefaultParametersDataViewIndex] = FStateTreeDataView(StateTree->GetDefaultParameters().GetMutableValue());	
	}
}

void FStateTreeExecutionContext::SetParameters(const FInstancedPropertyBag& Parameters)
{
	if (ensureMsgf(StateTree != nullptr, TEXT("Execution context must be initialized before calling %s"), ANSI_TO_TCHAR(__FUNCTION__))
		&& ensureMsgf(StateTree->GetDefaultParameters().GetPropertyBagStruct() == Parameters.GetPropertyBagStruct(),
			TEXT("Parameters must be of the same struct type. Make sure to migrate the provided parameters to the same type as the StateTree default parameters."))
		&& DataViews.IsValidIndex(StateTree->DefaultParametersDataViewIndex))
	{
		DataViews[StateTree->DefaultParametersDataViewIndex] = FStateTreeDataView(Parameters.GetMutableValue());
	}
}

void FStateTreeExecutionContext::Reset()
{
	InternalInstanceData.Reset();
	DataViews.Reset();
	StorageType = EStateTreeStorage::Internal;
	VisitedStates.Reset();
	StateTree = nullptr;
	Owner = nullptr;
}

void FStateTreeExecutionContext::UpdateLinkedStateParameters(FStateTreeInstanceData& InstanceData, const FCompactStateTreeState& State)
{
	check(State.ParameterInstanceIndex != MAX_uint16);
	
	const FStateTreeDataView StateParamsInstance = GetInstanceData(InstanceData, false, State.ParameterInstanceIndex);
	const FCompactStateTreeParameters& StateParams = StateParamsInstance.GetMutable<FCompactStateTreeParameters>();

	// Parameters property bag
	const FStateTreeDataView ParametersView(StateParams.Parameters.GetMutableValue());
	if (StateParams.BindingsBatch.IsValid())
	{
		StateTree->PropertyBindings.CopyTo(DataViews, StateParams.BindingsBatch.Index, ParametersView);
	}

	// Set the parameters as the input parameters for the linked state.
	check(State.LinkedState.IsValid());
	const FCompactStateTreeState& LinkedState = StateTree->States[State.LinkedState.Index];
	check(LinkedState.ParameterDataViewIndex != MAX_uint16);
	DataViews[LinkedState.ParameterDataViewIndex] = ParametersView;
}

void FStateTreeExecutionContext::UpdateSubtreeStateParameters(FStateTreeInstanceData& InstanceData, const FCompactStateTreeState& State)
{
	check(State.ParameterDataViewIndex != MAX_uint16);
	check(State.ParameterInstanceIndex != MAX_uint16);
	
	// Usually the subtree parameter view is set by the linked state. If it's not (i.e. transitioned into a parametrized subtree), we'll set the view default params.
	if (DataViews[State.ParameterDataViewIndex].IsValid())
	{
		return;
	}

	// Set view to default parameters.
	const FStateTreeDataView ParamInstanceView = GetInstanceData(InstanceData, false, State.ParameterInstanceIndex);
	const FCompactStateTreeParameters& Params = ParamInstanceView.GetMutable<FCompactStateTreeParameters>();
	DataViews[State.ParameterDataViewIndex] = FStateTreeDataView(Params.Parameters.GetMutableValue());
}

EStateTreeRunStatus FStateTreeExecutionContext::Start(FStateTreeInstanceData* ExternalInstanceData)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Start);

	if (!Owner || !StateTree)
	{
		return EStateTreeRunStatus::Failed;
	}
	
	FStateTreeInstanceData& InstanceData = SelectMutableInstanceData(ExternalInstanceData);
	checkSlow(InstanceData.GetLayout() == StateTree->GetInstanceDataDefaultValue().GetLayout());
	FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	// Stop if still running previous state.
	if (Exec.TreeRunStatus == EStateTreeRunStatus::Running)
	{
		FStateTreeTransitionResult Transition;
		Transition.TargetState = FStateTreeHandle::Succeeded;
		Transition.CurrentActiveStates = Exec.ActiveStates;
		Transition.CurrentRunStatus = Exec.LastTickStatus;
		Transition.NextActiveStates = FStateTreeActiveStates(FStateTreeHandle::Succeeded);

		ExitState(InstanceData, Transition);
	}
	
	// Initialize to unset running state.
	Exec.TreeRunStatus = EStateTreeRunStatus::Running;
	Exec.ActiveStates.Reset();
	Exec.LastTickStatus = EStateTreeRunStatus::Unset;

	// Select new Starting from root.
	VisitedStates.Init(false, StateTree->States.Num());
	
	static const FStateTreeHandle RootState = FStateTreeHandle(0);

	FStateTreeActiveStates NextActiveStates;
	if (SelectState(InstanceData, RootState, NextActiveStates))
	{
		if (NextActiveStates.Last() == FStateTreeHandle::Succeeded || NextActiveStates.Last() == FStateTreeHandle::Failed)
		{
			// Transition to a terminal state (succeeded/failed), or default transition failed.
			STATETREE_LOG(Warning, TEXT("%s: Tree %s at StateTree start on '%s' using StateTree '%s'."),
				ANSI_TO_TCHAR(__FUNCTION__), NextActiveStates.Last() == FStateTreeHandle::Succeeded ? TEXT("succeeded") : TEXT("failed"), *GetNameSafe(Owner), *GetNameSafe(StateTree));
			Exec.TreeRunStatus = NextActiveStates.Last() == FStateTreeHandle::Succeeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
		}
		else
		{
			// Enter state tasks can fail/succeed, treat it same as tick.
			FStateTreeTransitionResult Transition;
			Transition.TargetState = RootState;
			Transition.CurrentActiveStates = Exec.ActiveStates;
			Transition.CurrentRunStatus = Exec.LastTickStatus;
			Transition.NextActiveStates = NextActiveStates; // Enter state will update Exec.ActiveStates.
			Exec.LastTickStatus = EnterState(InstanceData, Transition);
			
			// Report state completed immediately.
			if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
			{
				StateCompleted(InstanceData);
			}
		}
	}

	if (Exec.ActiveStates.IsEmpty())
	{
		// Should not happen. This may happen if initial state could not be selected.
		STATETREE_LOG(Error, TEXT("%s: Failed to select initial state on '%s' using StateTree '%s'. This should not happen, check that the StateTree logic can always select a state at start."),
			ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(Owner), *GetNameSafe(StateTree));
		Exec.TreeRunStatus = EStateTreeRunStatus::Failed;
	}

	return Exec.TreeRunStatus;
}

EStateTreeRunStatus FStateTreeExecutionContext::Stop(FStateTreeInstanceData* ExternalInstanceData)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Stop);

	if (!Owner || !StateTree)
	{
		return EStateTreeRunStatus::Failed;
	}
	
	FStateTreeInstanceData& InstanceData = SelectMutableInstanceData(ExternalInstanceData);
	checkSlow(InstanceData.GetLayout() == StateTree->GetInstanceDataDefaultValue().GetLayout());
	FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	// Exit states if still in some valid state.
	if (!Exec.ActiveStates.IsEmpty() && (Exec.ActiveStates.Last() != FStateTreeHandle::Succeeded || Exec.ActiveStates.Last() != FStateTreeHandle::Failed))
	{
		// Transition to Succeeded state.
		FStateTreeTransitionResult Transition;
		Transition.TargetState = FStateTreeHandle::Succeeded;
		Transition.CurrentActiveStates = Exec.ActiveStates;
		Transition.CurrentRunStatus = Exec.LastTickStatus;
		Transition.NextActiveStates = FStateTreeActiveStates(FStateTreeHandle::Succeeded);

		ExitState(InstanceData, Transition);

		Exec.TreeRunStatus = EStateTreeRunStatus::Succeeded;
	}
	else
	{
		Exec.TreeRunStatus = Exec.ActiveStates.Last() == FStateTreeHandle::Succeeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
	}

	Exec.ActiveStates.Reset();

	Exec.LastTickStatus = EStateTreeRunStatus::Unset;

	return Exec.TreeRunStatus;
}

EStateTreeRunStatus FStateTreeExecutionContext::Tick(const float DeltaTime, FStateTreeInstanceData* ExternalInstanceData)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Tick);

	if (!Owner || !StateTree)
	{
		return EStateTreeRunStatus::Failed;
	}
	FStateTreeInstanceData& InstanceData = SelectMutableInstanceData(ExternalInstanceData);
	checkSlow(InstanceData.GetLayout() == StateTree->GetInstanceDataDefaultValue().GetLayout());
	FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	// No ticking of the tree is done or stopped.
	if (Exec.TreeRunStatus != EStateTreeRunStatus::Running)
	{
		return Exec.TreeRunStatus;
	}

	// Update the gated transition time.
	if (Exec.GatedTransitionIndex != INDEX_NONE)
	{
		Exec.GatedTransitionTime -= DeltaTime;
	}
	
	// Reset visited state, used to keep track which state's evaluators have been ticked.
	VisitedStates.Init(false, StateTree->States.Num());

	if (Exec.LastTickStatus == EStateTreeRunStatus::Running)
	{
		// Tick evaluators on active states. Further evaluator may be ticked during selection as non-active states are visited.
		// Ticked states are kept track of and evaluators are ticked just once per frame.
		TickEvaluators(InstanceData, Exec.ActiveStates, EStateTreeEvaluationType::Tick, DeltaTime);

		// Tick tasks on active states.
		Exec.LastTickStatus = TickTasks(InstanceData, Exec, DeltaTime);
		
		// Report state completed immediately.
		if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
		{
			StateCompleted(InstanceData);
		}
	}

	// The state selection is repeated up to MaxIteration time. This allows failed EnterState() to potentially find a new state immediately.
	// This helps event driven StateTrees to not require another event/tick to find a suitable state.
	static constexpr int32 MaxIterations = 5;
	for (int32 Iter = 0; Iter < MaxIterations; Iter++)
	{
		// Trigger conditional transitions or state succeed/failed transitions. First tick transition is handled here too.
		FStateTreeTransitionResult Transition;
		if (TriggerTransitions(InstanceData, Transition))
		{
			ExitState(InstanceData, Transition);
			
			if (Transition.NextActiveStates.Last() == FStateTreeHandle::Succeeded || Transition.NextActiveStates.Last() == FStateTreeHandle::Failed)
			{
				// Transition to a terminal state (succeeded/failed), or default transition failed.
				Exec.TreeRunStatus = Transition.NextActiveStates.Last() == FStateTreeHandle::Succeeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
				return Exec.TreeRunStatus;
			}
			
			// Enter state tasks can fail/succeed, treat it same as tick.
			Exec.LastTickStatus = EnterState(InstanceData, Transition);

			// Report state completed immediately.
			if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
			{
				StateCompleted(InstanceData);
			}
		}
		
		// Stop as soon as have found a running state.
		if (Exec.LastTickStatus == EStateTreeRunStatus::Running)
		{
			break;
		}
		
		// Reset visited states to allow evaluators to update during the next transition selection.
		VisitedStates.Init(false, StateTree->States.Num());
	}
	
	if (Exec.ActiveStates.IsEmpty())
	{
		// Should not happen. This may happen if a state completion transition could not be selected. 
		STATETREE_LOG(Error, TEXT("%s: Failed to select state on '%s' using StateTree '%s'. This should not happen, state completion transition is likely missing."),
			ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(Owner), *GetNameSafe(StateTree));
		Exec.TreeRunStatus = EStateTreeRunStatus::Failed;
		return Exec.TreeRunStatus;
	}

	return Exec.TreeRunStatus;
}

EStateTreeRunStatus FStateTreeExecutionContext::EnterState(FStateTreeInstanceData& InstanceData, const FStateTreeTransitionResult& Transition)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_EnterState);

	if (Transition.NextActiveStates.IsEmpty())
	{
		return EStateTreeRunStatus::Failed;
	}

	FStateTreeExecutionState& Exec = GetExecState(InstanceData);
	Exec.StateChangeCount++;

	// On target branch means that the state is the target of current transition or child of it.
	// States which were active before and will remain active, but are not on target branch will not get
	// EnterState called. That is, a transition is handled as "replan from this state".
	bool bOnTargetBranch = false;

	FStateTreeTransitionResult CurrentTransition = Transition;
	
	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;

	Exec.EnterStateFailedTaskIndex = MAX_uint16; // This will make all tasks to be accepted.
	Exec.ActiveStates.Reset();
	
	// Do property copy on all states, propagating the results from last tick.
	for (int32 Index = 0; Index < Transition.NextActiveStates.Num() && Result != EStateTreeRunStatus::Failed; Index++)
	{
		const FStateTreeHandle CurrentHandle = Transition.NextActiveStates[Index];
		const FStateTreeHandle PreviousHandle = Transition.CurrentActiveStates.GetStateSafe(Index);
		const FCompactStateTreeState& State = StateTree->States[CurrentHandle.Index];

		if (!Exec.ActiveStates.Push(CurrentHandle))
		{
			STATETREE_LOG(Error, TEXT("%s: Reached max execution depth when trying to enter state '%s'.  '%s' using StateTree '%s'."),
				ANSI_TO_TCHAR(__FUNCTION__), *GetStateStatusString(Exec), *GetNameSafe(Owner), *GetNameSafe(StateTree));
			break;
		}

		if (State.Type == EStateTreeStateType::Linked)
		{
			UpdateLinkedStateParameters(InstanceData, State);
		}
		else if (State.Type == EStateTreeStateType::Subtree)
		{
			UpdateSubtreeStateParameters(InstanceData, State);
		}

		bOnTargetBranch = bOnTargetBranch || CurrentHandle == Transition.TargetState;
		const bool bWasActive = PreviousHandle == CurrentHandle;
		const bool bIsEnteringState = !bWasActive || bOnTargetBranch;

		CurrentTransition.CurrentState = CurrentHandle;
		
		const EStateTreeStateChangeType ChangeType = bWasActive ? EStateTreeStateChangeType::Sustained : EStateTreeStateChangeType::Changed;

		STATETREE_CLOG(bIsEnteringState, Log, TEXT("%*sEnter state '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(Transition.NextActiveStates, Index), *UEnum::GetValueAsString(ChangeType));
		
		for (int32 EvalIndex = State.EvaluatorsBegin; EvalIndex < (int32)(State.EvaluatorsBegin + State.EvaluatorsNum); EvalIndex++)
		{
			const FStateTreeEvaluatorBase& Eval = GetNode<FStateTreeEvaluatorBase>(EvalIndex);
			const FStateTreeDataView EvalInstanceView = GetInstanceData(InstanceData, Eval.bInstanceIsObject, Eval.InstanceIndex);
			DataViews[Eval.DataViewIndex] = EvalInstanceView;

			// Copy bound properties.
			if (Eval.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(DataViews, Eval.BindingsBatch.Index, EvalInstanceView);
			}

			if (bIsEnteringState)
			{
				STATETREE_LOG(Verbose, TEXT("%*s  Notify Evaluator '%s'."), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Eval.Name.ToString());
				QUICK_SCOPE_CYCLE_COUNTER(StateTree_Eval_EnterState);
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Eval_EnterState);
				Eval.EnterState(*this, ChangeType, CurrentTransition);
			}
		}
		
		// Activate tasks on current state.
		for (int32 TaskIndex = State.TasksBegin; TaskIndex < (int32)(State.TasksBegin + State.TasksNum); TaskIndex++)
		{
			const FStateTreeTaskBase& Task = GetNode<FStateTreeTaskBase>(TaskIndex);
			const FStateTreeDataView TaskInstanceView = GetInstanceData(InstanceData, Task.bInstanceIsObject, Task.InstanceIndex);
			DataViews[Task.DataViewIndex] = TaskInstanceView;

			// Copy bound properties.
			if (Task.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(DataViews, Task.BindingsBatch.Index, TaskInstanceView);
			}

			if (bIsEnteringState)
			{
				STATETREE_LOG(Verbose, TEXT("%*s  Notify Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
				QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_EnterState);
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Task_EnterState);
				const EStateTreeRunStatus Status = Task.EnterState(*this, ChangeType, CurrentTransition);
				
				if (Status == EStateTreeRunStatus::Failed)
				{
					// Store how far in the enter state we got. This will be used to match the ExitState() calls.
					Exec.EnterStateFailedTaskIndex = TaskIndex;
					Result = Status;
					break;
				}
			}
		}
	}

	return Result;
}

void FStateTreeExecutionContext::ExitState(FStateTreeInstanceData& InstanceData, const FStateTreeTransitionResult& Transition)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_ExitState);

	if (Transition.CurrentActiveStates.IsEmpty())
	{
		return;
	}

	// Reset transition delay
	FStateTreeExecutionState& Exec = GetExecState(InstanceData);
	Exec.GatedTransitionIndex = INDEX_NONE;
	Exec.GatedTransitionTime = 0.0f;

	// On target branch means that the state is the target of current transition or child of it.
	// States which were active before and will remain active, but are not on target branch will not get
	// EnterState called. That is, a transition is handled as "replan from this state".
	bool bOnTargetBranch = false;

	FStateTreeHandle ExitedStates[FStateTreeActiveStates::MaxStates];
	EStateTreeStateChangeType ExitedStateChangeType[FStateTreeActiveStates::MaxStates];
	int32 ExitedStateActiveIndex[FStateTreeActiveStates::MaxStates];
	int32 NumExitedStates = 0;
	
	// Do property copy on all states, propagating the results from last tick.
	// Collect the states that need to be called, the actual call is done below in reverse order.
	for (int32 Index = 0; Index < Transition.CurrentActiveStates.Num(); Index++)
	{
		const FStateTreeHandle CurrentHandle = Transition.CurrentActiveStates[Index];
		const FStateTreeHandle NextHandle = Transition.NextActiveStates.GetStateSafe(Index);
		const FCompactStateTreeState& State = StateTree->States[CurrentHandle.Index];

		if (State.Type == EStateTreeStateType::Linked)
		{
			UpdateLinkedStateParameters(InstanceData, State);
		}
		else if (State.Type == EStateTreeStateType::Subtree)
		{
			UpdateSubtreeStateParameters(InstanceData, State);
		}

		const bool bRemainsActive = NextHandle == CurrentHandle;
		bOnTargetBranch = bOnTargetBranch || NextHandle == Transition.TargetState;
		const EStateTreeStateChangeType ChangeType = bRemainsActive ? EStateTreeStateChangeType::Sustained : EStateTreeStateChangeType::Changed;

		if (!bRemainsActive || bOnTargetBranch)
		{
			// Should call ExitState() on this state.
			check (NumExitedStates < FStateTreeActiveStates::MaxStates);
			ExitedStates[NumExitedStates] = CurrentHandle;
			ExitedStateChangeType[NumExitedStates] = ChangeType;
			ExitedStateActiveIndex[NumExitedStates] = Index;
			NumExitedStates++;
		}

		// Do property copies, ExitState() is called below.
		for (int32 EvalIndex = State.EvaluatorsBegin; EvalIndex < (int32)(State.EvaluatorsBegin + State.EvaluatorsNum); EvalIndex++)
		{
			const FStateTreeEvaluatorBase& Eval = GetNode<FStateTreeEvaluatorBase>(EvalIndex);
			const FStateTreeDataView EvalInstanceView = GetInstanceData(InstanceData, Eval.bInstanceIsObject, Eval.InstanceIndex);
			DataViews[Eval.DataViewIndex] = EvalInstanceView;

			// Copy bound properties.
			if (Eval.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(DataViews, Eval.BindingsBatch.Index, EvalInstanceView);
			}
		}

		for (int32 TaskIndex = State.TasksBegin; TaskIndex < (int32)(State.TasksBegin + State.TasksNum); TaskIndex++)
		{
			const FStateTreeTaskBase& Task = GetNode<FStateTreeTaskBase>(TaskIndex);
			const FStateTreeDataView TaskInstanceView = GetInstanceData(InstanceData, Task.bInstanceIsObject, Task.InstanceIndex);
			DataViews[Task.DataViewIndex] = TaskInstanceView;

			// Copy bound properties.
			if (Task.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(DataViews, Task.BindingsBatch.Index, TaskInstanceView);
			}
		}
	}

	// Call in reverse order.
	FStateTreeTransitionResult CurrentTransition = Transition;

	for (int32 Index = NumExitedStates - 1; Index >= 0; Index--)
	{
		const FStateTreeHandle CurrentHandle = ExitedStates[Index];
		const FCompactStateTreeState& State = StateTree->States[CurrentHandle.Index];
		const EStateTreeStateChangeType ChangeType = ExitedStateChangeType[Index];

		STATETREE_LOG(Log, TEXT("%*sExit state '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(Transition.CurrentActiveStates, ExitedStateActiveIndex[Index]), *UEnum::GetValueAsString(ChangeType));

		CurrentTransition.CurrentState = CurrentHandle;

		// Tasks
		for (int32 TaskIndex = (int32)(State.TasksBegin + State.TasksNum) - 1; TaskIndex >= State.TasksBegin; TaskIndex--)
		{
			// Call task completed only if EnterState() was called.
			// The task order in the tree (BF) allows us to use the comparison.
			if (TaskIndex <= (int32)Exec.EnterStateFailedTaskIndex)
			{
				const FStateTreeTaskBase& Task = GetNode<FStateTreeTaskBase>(TaskIndex);

				STATETREE_LOG(Verbose, TEXT("%*s  Notify Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
				{
					QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_ExitState);
					CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Task_ExitState);
					Task.ExitState(*this, ChangeType, CurrentTransition);
				}
			}
		}

		// Evaluators
		for (int32 EvalIndex = (int32)(State.EvaluatorsBegin + State.EvaluatorsNum) - 1; EvalIndex >= State.EvaluatorsBegin; EvalIndex--)
		{
			const FStateTreeEvaluatorBase& Eval = GetNode<FStateTreeEvaluatorBase>(EvalIndex);

			STATETREE_LOG(Verbose, TEXT("%*s  Notify Evaluator '%s'."), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Eval.Name.ToString());
			{
				QUICK_SCOPE_CYCLE_COUNTER(StateTree_Eval_ExitState);
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Eval_ExitState);
				Eval.ExitState(*this, ChangeType, CurrentTransition);
			}
		}
	}
}

void FStateTreeExecutionContext::StateCompleted(FStateTreeInstanceData& InstanceData)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_StateCompleted);

	const FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	if (Exec.ActiveStates.IsEmpty())
	{
		return;
	}

	// Call from child towards root to allow to pass results back.
	// Note: Completed is assumed to be called immediately after tick or enter state, so there's no property copying.
	for (int32 Index = Exec.ActiveStates.Num() - 1; Index >= 0; Index--)
	{
		const FStateTreeHandle CurrentHandle = Exec.ActiveStates[Index];
		const FCompactStateTreeState& State = StateTree->States[CurrentHandle.Index];

		STATETREE_LOG(Verbose, TEXT("%*sState Completed '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(Exec.ActiveStates, Index), *UEnum::GetValueAsString(Exec.LastTickStatus));

		// Notify Tasks
		for (int32 TaskIndex = (int32)(State.TasksBegin + State.TasksNum) - 1; TaskIndex >= State.TasksBegin; TaskIndex--)
		{
			// Call task completed only if EnterState() was called.
			// The task order in the tree (BF) allows us to use the comparison.
			if (TaskIndex <= (int32)Exec.EnterStateFailedTaskIndex)
			{
				const FStateTreeTaskBase& Task = GetNode<FStateTreeTaskBase>(TaskIndex);

				STATETREE_LOG(Verbose, TEXT("%*s  Notify Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
				Task.StateCompleted(*this, Exec.LastTickStatus, Exec.ActiveStates);
			}
		}

		// Notify evaluators
		for (int32 EvalIndex = (int32)(State.EvaluatorsBegin + State.EvaluatorsNum) - 1; EvalIndex >= State.EvaluatorsBegin; EvalIndex--)
		{
			const FStateTreeEvaluatorBase& Eval = GetNode<FStateTreeEvaluatorBase>(EvalIndex);

			STATETREE_LOG(Verbose, TEXT("%*s  Notify Evaluator '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Eval.Name.ToString());
			Eval.StateCompleted(*this, Exec.LastTickStatus, Exec.ActiveStates);
		}
	}
}

void FStateTreeExecutionContext::TickEvaluatorsForSelect(FStateTreeInstanceData& InstanceData, const FStateTreeHandle CurrentState, const EStateTreeEvaluationType EvalType, const float DeltaTime)
{
	if (CurrentState == FStateTreeHandle::Succeeded || CurrentState == FStateTreeHandle::Failed || CurrentState == FStateTreeHandle::Invalid)
	{
		return;
	}

	FStateTreeActiveStates Branch;
	FStateTreeHandle StateHandle = CurrentState;
	while (StateHandle.IsValid())
	{
		if (!Branch.PushFront(StateHandle))
		{
			break;
		}
		StateHandle = StateTree->States[StateHandle.Index].Parent;
	}

	TickEvaluators(InstanceData, Branch, EvalType, DeltaTime);
}

void FStateTreeExecutionContext::TickEvaluators(FStateTreeInstanceData& InstanceData, const FStateTreeActiveStates& ActiveStates, const EStateTreeEvaluationType EvalType, const float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_TickEvaluators);

	for (int32 Index = 0; Index < ActiveStates.Num(); Index++)
	{
		const FStateTreeHandle StateHandle = ActiveStates[Index];

		if (VisitedStates[StateHandle.Index])
		{
			// Already ticked this frame.
			continue;
		}

		const FCompactStateTreeState& State = StateTree->States[StateHandle.Index];

		if (State.Type == EStateTreeStateType::Linked)
		{
			UpdateLinkedStateParameters(InstanceData, State);
		}
		else if (State.Type == EStateTreeStateType::Subtree)
		{
			UpdateSubtreeStateParameters(InstanceData, State);
		}
		
		STATETREE_CLOG(State.EvaluatorsNum > 0, Verbose, TEXT("%*sTicking Evaluators of state '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(ActiveStates, Index), *UEnum::GetValueAsString(EvalType));

		// Tick evaluators
		for (int32 EvalIndex = 0; EvalIndex < int32(State.EvaluatorsNum); EvalIndex++)
		{
			const FStateTreeEvaluatorBase& Eval = GetNode<FStateTreeEvaluatorBase>(int32(State.EvaluatorsBegin) + EvalIndex);
			const FStateTreeDataView EvalInstanceView = GetInstanceData(InstanceData, Eval.bInstanceIsObject, Eval.InstanceIndex);
			DataViews[Eval.DataViewIndex] = EvalInstanceView;

			// Copy bound properties.
			if (Eval.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(DataViews, Eval.BindingsBatch.Index, EvalInstanceView);
			}
			STATETREE_LOG(Verbose, TEXT("%*s  Evaluate: '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Eval.Name.ToString());
			{
				QUICK_SCOPE_CYCLE_COUNTER(StateTree_Eval_Tick);
				Eval.Evaluate(*this, EvalType, DeltaTime);
			}
		}

		VisitedStates[StateHandle.Index] = true;
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::TickTasks(FStateTreeInstanceData& InstanceData, const FStateTreeExecutionState& Exec, const float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_TickTasks);

	if (Exec.ActiveStates.IsEmpty())
	{
		return EStateTreeRunStatus::Failed;
	}

	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;
	int32 NumTotalTasks = 0;

	for (int32 Index = 0; Index < Exec.ActiveStates.Num() && Result != EStateTreeRunStatus::Failed; Index++)
	{
		const FStateTreeHandle CurrentHandle = Exec.ActiveStates[Index];
		const FCompactStateTreeState& State = StateTree->States[CurrentHandle.Index];

		STATETREE_CLOG(State.TasksNum > 0, Verbose, TEXT("%*sTicking Tasks of state '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(Exec.ActiveStates, Index));

		if (State.Type == EStateTreeStateType::Linked)
		{
			UpdateLinkedStateParameters(InstanceData, State);
		}
		else if (State.Type == EStateTreeStateType::Subtree)
		{
			UpdateSubtreeStateParameters(InstanceData, State);
		}

		// Tick Tasks
		for (int32 TaskIndex = 0; TaskIndex < int32(State.TasksNum); TaskIndex++)
		{
			const FStateTreeTaskBase& Task = GetNode<FStateTreeTaskBase>(int32(State.TasksBegin) + TaskIndex);
			const FStateTreeDataView TaskInstanceView = GetInstanceData(InstanceData, Task.bInstanceIsObject, Task.InstanceIndex);
			DataViews[Task.DataViewIndex] = TaskInstanceView;

			// Copy bound properties.
			if (Task.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(DataViews, Task.BindingsBatch.Index, TaskInstanceView);
			}
			STATETREE_LOG(Verbose, TEXT("%*s  Tick: '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
			
			{
				QUICK_SCOPE_CYCLE_COUNTER(StateTree_Task_Tick);
				CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_Task_Tick);

				const EStateTreeRunStatus TaskResult = Task.Tick(*this, DeltaTime);

				// TODO: Add more control over which states can control the failed/succeeded result.
				if (TaskResult != EStateTreeRunStatus::Running)
				{
					Result = TaskResult;
				}
				if (TaskResult == EStateTreeRunStatus::Failed)
				{
					break;
				}
			}
		}
		NumTotalTasks += State.TasksNum;
	}

	if (NumTotalTasks == 0)
	{
		// No tasks, done ticking.
		Result = EStateTreeRunStatus::Succeeded;
	}

	return Result;
}

bool FStateTreeExecutionContext::TestAllConditions(const uint32 ConditionsOffset, const uint32 ConditionsNum)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_TestConditions);

	if (ConditionsNum == 0)
	{
		return true;
	}

	const FStateTreeInstanceData& SharedInstanceData = StateTree->GetSharedInstanceData();
	
	TStaticArray<EStateTreeConditionOperand, UE::StateTree::MaxConditionIndent + 1> Operands(InPlace, EStateTreeConditionOperand::Copy);
	TStaticArray<bool, UE::StateTree::MaxConditionIndent + 1> Values(InPlace, false);

	int32 Level = 0;
	
	for (uint32 Index = 0; Index < ConditionsNum; Index++)
	{
		const FStateTreeConditionBase& Cond = GetNode<FStateTreeConditionBase>(ConditionsOffset + Index);
		const FStateTreeDataView CondInstanceView = GetInstanceData(SharedInstanceData, Cond.bInstanceIsObject, Cond.InstanceIndex);
		DataViews[Cond.DataViewIndex] = CondInstanceView;

		// Copy bound properties.
		if (Cond.BindingsBatch.IsValid())
		{
			if (!StateTree->PropertyBindings.CopyTo(DataViews, Cond.BindingsBatch.Index, CondInstanceView))
			{
				// If the source data cannot be accessed, the whole expression evaluates to false.
				Values[0] = false;
				break;
			}
		}

		const bool bValue = Cond.TestCondition(*this);

		const int32 DeltaIndent = Cond.DeltaIndent;
		const int32 OpenParens = FMath::Max(0, DeltaIndent) + 1;	// +1 for the current value that is stored at the empty slot at the top of the value stack.
		const int32 ClosedParens = FMath::Max(0, -DeltaIndent) + 1;

		// Store the operand to apply when merging higher level down when returning to this level.
		// @todo: remove this conditions in 5.1, needs resaving existing StateTrees.
		const EStateTreeConditionOperand Operand = Index == 0 ? EStateTreeConditionOperand::Copy : Cond.Operand;
		Operands[Level] = Operand;

		// Store current value at the top of the stack.
		Level += OpenParens;
		Values[Level] = bValue;

		// Evaluate and merge down values based on closed braces.
		// The current value is placed in parens (see +1 above), which makes merging down and applying the new value consistent.
		// The default operand is copy, so if the value is needed immediately, it is just copied down, or if we're on the same level,
		// the operand storing above gives handles with the right logic.
		for (int32 Paren = 0; Paren < ClosedParens; Paren++)
		{
			Level--;
			switch (Operands[Level])
			{
			case EStateTreeConditionOperand::Copy:
				Values[Level] = Values[Level + 1];
				break;
			case EStateTreeConditionOperand::And:
				Values[Level] &= Values[Level + 1];
				break;
			case EStateTreeConditionOperand::Or:
				Values[Level] |= Values[Level + 1];
				break;
			}
			Operands[Level] = EStateTreeConditionOperand::Copy;
		}
	}
	
	return Values[0];
}

bool FStateTreeExecutionContext::TriggerTransitions(FStateTreeInstanceData& InstanceData, FStateTreeTransitionResult& OutTransition)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_TriggerTransition);

	FStateTreeExecutionState& Exec = GetExecState(InstanceData);
	EStateTreeTransitionEvent Event = EStateTreeTransitionEvent::OnCondition;
	if (Exec.LastTickStatus == EStateTreeRunStatus::Succeeded)
	{
		Event = EStateTreeTransitionEvent::OnSucceeded;
	}
	else if (Exec.LastTickStatus == EStateTreeRunStatus::Failed)
	{
		Event = EStateTreeTransitionEvent::OnFailed;
	}

	// Walk towards root and check all transitions along the way.
	for (int32 StateIndex = Exec.ActiveStates.Num() - 1; StateIndex >= 0; StateIndex--)
	{
		const FCompactStateTreeState& State = StateTree->States[Exec.ActiveStates[StateIndex].Index];
		
		for (uint8 i = 0; i < State.TransitionsNum; i++)
		{
			// All transition conditions must pass
			const int16 TransitionIndex = State.TransitionsBegin + i;
			const FCompactStateTransition& Transition = StateTree->Transitions[TransitionIndex];
			if (EnumHasAllFlags(Transition.Event, Event) && TestAllConditions(Transition.ConditionsBegin, Transition.ConditionsNum))
			{
				// If a transition has delay, we stop testing other transitions, but the transition will not pass the condition until the delay time passes.
				if (Transition.GateDelay > 0)
				{
					if (Exec.GatedTransitionIndex != TransitionIndex)
					{
						Exec.GatedTransitionIndex = TransitionIndex;
						Exec.GatedTransitionTime = FMath::RandRange(0.0f, Transition.GateDelay * 0.1f); // TODO: we need variance too.
						BeginGatedTransition(Exec);
						STATETREE_LOG(Verbose, TEXT("Gated transition triggered from '%s' (%s) -> '%s' %.1fs"), *GetSafeStateName(Exec.ActiveStates.Last()), *State.Name.ToString(), *GetSafeStateName(Transition.State), Exec.GatedTransitionTime);
					}

					// Keep on updating current state, until we have tried to trigger
					if (Exec.GatedTransitionTime > 0.0f)
					{
						return false;
					}

					STATETREE_LOG(Verbose, TEXT("Passed gated transition from '%s' (%s) -> '%s'"), *GetSafeStateName(Exec.ActiveStates.Last()), *State.Name.ToString(), *GetSafeStateName(Transition.State));
				}
				
				if (Transition.Type == EStateTreeTransitionType::GotoState || Transition.Type == EStateTreeTransitionType::NextState)
				{
					OutTransition.CurrentActiveStates = Exec.ActiveStates;
					OutTransition.TargetState = Transition.State;
					OutTransition.NextActiveStates.Reset();

					if (SelectState(InstanceData, Transition.State, OutTransition.NextActiveStates))
					{
						STATETREE_LOG(Verbose, TEXT("Transition on state '%s' (%s) -[%s]-> state '%s'"), *GetSafeStateName(Exec.ActiveStates.Last()), *State.Name.ToString(), *GetSafeStateName(Transition.State), *GetSafeStateName(OutTransition.NextActiveStates.Last()));
						return true;
					}
				}
				else if (Transition.Type == EStateTreeTransitionType::NotSet)
				{
					// NotSet is no-operation, but can be used to mask a transition at parent state. Returning unset keeps updating current state.
					return false;
				}
				else if (Transition.Type == EStateTreeTransitionType::Succeeded)
				{
					STATETREE_LOG(Verbose, TEXT("Stop tree execution from state '%s' (%s): Succeeded"), *GetSafeStateName(Exec.ActiveStates.Last()), *State.Name.ToString());
					OutTransition.CurrentActiveStates = Exec.ActiveStates;
					OutTransition.TargetState = FStateTreeHandle::Succeeded;
					OutTransition.NextActiveStates = FStateTreeActiveStates(FStateTreeHandle::Succeeded);
					return true;
				}
				else
				{
					STATETREE_LOG(Verbose, TEXT("Stop tree execution from state '%s' (%s): Failed"), *GetSafeStateName(Exec.ActiveStates.Last()), *State.Name.ToString());
					OutTransition.CurrentActiveStates = Exec.ActiveStates;
					OutTransition.TargetState = FStateTreeHandle::Failed;
					OutTransition.NextActiveStates = FStateTreeActiveStates(FStateTreeHandle::Failed);
					return true;
				}
			}
			else if (Exec.GatedTransitionIndex == TransitionIndex)
			{
				// If the current transition was gated transition, reset it if the condition failed.
				Exec.GatedTransitionIndex = INDEX_NONE;
				Exec.GatedTransitionTime = 0.0f;
			}
		}
	}

	if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
	{
		// Could not trigger completion transition, jump back to start.
		static const FStateTreeHandle RootState = FStateTreeHandle(0);
		OutTransition.TargetState = RootState;
		return SelectState(InstanceData, RootState, OutTransition.NextActiveStates);
	}

	// No transition triggered, keep on updating current state.
	return false;
}

bool FStateTreeExecutionContext::SelectState(FStateTreeInstanceData& InstanceData, const FStateTreeHandle NextState, FStateTreeActiveStates& OutNewActiveState)
{
	const FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	if (!NextState.IsValid())
	{
		return false;
	}

	// Find common ancestor of `NextState` in the current active states and connect.
	// This allows transitions within a subtree.
	OutNewActiveState = Exec.ActiveStates;
	
	TStaticArray<FStateTreeHandle, FStateTreeActiveStates::MaxStates> InBetweenStates;
	int32 NumInBetweenStates = 0;
	int32 CommonActiveAncestorIndex = INDEX_NONE;

	// Walk towards the root from current state.
	FStateTreeHandle CurrState = NextState;
	while (CurrState.IsValid())
	{
		// Store the states that are in between the 'NextState' and common ancestor. 
		InBetweenStates[NumInBetweenStates++] = CurrState;
		// Check if the state can be found in the active states.
		CommonActiveAncestorIndex = OutNewActiveState.IndexOfReverse(CurrState); 
		if (CommonActiveAncestorIndex != INDEX_NONE)
		{
			break;
		}
		if (NumInBetweenStates == InBetweenStates.Num())
		{
			STATETREE_LOG(Error, TEXT("%s: Too many parent states when selecting state '%s' from '%s'.  '%s' using StateTree '%s'."),
				ANSI_TO_TCHAR(__FUNCTION__), *GetSafeStateName(NextState), *GetStateStatusString(Exec), *GetNameSafe(Owner), *GetNameSafe(StateTree));
			return false;
		}

		CurrState = StateTree->States[CurrState.Index].Parent;
	}

	// Max takes care of INDEX_NONE, by setting the num to 0.
	OutNewActiveState.SetNum(FMath::Max(0, CommonActiveAncestorIndex));
	
	// Append in between state in reverse order, they were collected from leaf towards the root.
	bool bActiveStatesOverflow = false;
	for (int32 Index = NumInBetweenStates - 1; Index > 0; Index--)
	{
		bActiveStatesOverflow |= !OutNewActiveState.Push(InBetweenStates[Index]);
	}

	if (bActiveStatesOverflow)
	{
		STATETREE_LOG(Error, TEXT("%s: Reached max execution depth when trying to select state %s from '%s'.  '%s' using StateTree '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), *GetSafeStateName(NextState), *GetStateStatusString(Exec), *GetNameSafe(Owner), *GetNameSafe(StateTree));
		return false;
	}

	return SelectStateInternal(InstanceData, NextState, OutNewActiveState);
}

bool FStateTreeExecutionContext::SelectStateInternal(FStateTreeInstanceData& InstanceData, const FStateTreeHandle NextState, FStateTreeActiveStates& OutNewActiveState)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTree_SelectState);

	const FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	if (!NextState.IsValid())
	{
		// Trying to select non-existing state.
		STATETREE_LOG(Error, TEXT("%s: Trying to select invalid state from '%s'.  '%s' using StateTree '%s'."),
            ANSI_TO_TCHAR(__FUNCTION__), *GetStateStatusString(Exec), *GetNameSafe(Owner), *GetNameSafe(StateTree));
		return false;
	}

	const FCompactStateTreeState& State = StateTree->States[NextState.Index];

	// Make sure all the evaluators for the target state are up to date.
	TickEvaluatorsForSelect(InstanceData, NextState, EStateTreeEvaluationType::PreSelect, 0.0f);
	
	// Check that the state can be entered
	if (TestAllConditions(State.EnterConditionsBegin, State.EnterConditionsNum))
	{
		if (!OutNewActiveState.Push(NextState))
		{
			STATETREE_LOG(Error, TEXT("%s: Reached max execution depth when trying to select state %s from '%s'.  '%s' using StateTree '%s'."),
				ANSI_TO_TCHAR(__FUNCTION__), *GetSafeStateName(NextState), *GetStateStatusString(Exec), *GetNameSafe(Owner), *GetNameSafe(StateTree));
			return false;
		}
		
		if (State.LinkedState.IsValid())
		{
			// If State is linked, proceed to the linked state.
			if (SelectStateInternal(InstanceData, State.LinkedState, OutNewActiveState))
			{
				// Selection succeeded
				return true;
			}
		}
		else if (State.HasChildren())
		{
			// If the state has children, proceed to select children.
			for (uint16 ChildState = State.ChildrenBegin; ChildState < State.ChildrenEnd; ChildState = StateTree->States[ChildState].GetNextSibling())
			{
				if (SelectStateInternal(InstanceData, FStateTreeHandle(ChildState), OutNewActiveState))
				{
					// Selection succeeded
					return true;
				}
			}
		}
		else
		{
			// Select this state.
			return true;
		}
		
		OutNewActiveState.Pop();
	}

	// Nothing got selected.
	return false;
}

FString FStateTreeExecutionContext::GetSafeStateName(const FStateTreeHandle State) const
{
	check(StateTree);
	if (State == FStateTreeHandle::Invalid)
	{
		return TEXT("(State Invalid)");
	}
	else if (State == FStateTreeHandle::Succeeded)
	{
		return TEXT("(State Succeeded)");
	}
	else if (State == FStateTreeHandle::Failed)
	{
		return TEXT("(State Failed)");
	}
	else if (StateTree->States.IsValidIndex(State.Index))
	{
		return *StateTree->States[State.Index].Name.ToString();
	}
	return TEXT("(Unknown)");
}

FString FStateTreeExecutionContext::DebugGetStatePath(const FStateTreeActiveStates& ActiveStates, const int32 ActiveStateIndex) const
{
	FString StatePath;
	if (!ensureMsgf(ActiveStates.IsValidIndex(ActiveStateIndex), TEXT("Provided index must be valid")))
	{
		return StatePath;
	}

	for (int32 i = 0; i <= ActiveStateIndex; i++)
	{
		const FCompactStateTreeState& State = StateTree->States[ActiveStates[i].Index];
		StatePath.Appendf(TEXT("%s%s"), i == 0 ? TEXT("") : TEXT("."), *State.Name.ToString());
	}
	return StatePath;
}

FString FStateTreeExecutionContext::GetStateStatusString(const FStateTreeExecutionState& ExecState) const
{
	return GetSafeStateName(ExecState.ActiveStates.Last()) + TEXT(":") + UEnum::GetDisplayValueAsText(ExecState.LastTickStatus).ToString();
}

EStateTreeRunStatus FStateTreeExecutionContext::GetLastTickStatus(const FStateTreeInstanceData* ExternalInstanceData) const
{
	const FStateTreeExecutionState& Exec = GetExecState(SelectInstanceData(ExternalInstanceData));
	return Exec.LastTickStatus;
}

FString FStateTreeExecutionContext::GetInstanceDescription() const
{
	return Owner != nullptr? FString::Printf(TEXT("%s: "), *Owner->GetName()) : TEXT("");
}

const FStateTreeActiveStates& FStateTreeExecutionContext::GetActiveStates(const FStateTreeInstanceData* ExternalInstanceData) const
{
	const FStateTreeExecutionState& Exec = GetExecState(SelectInstanceData(ExternalInstanceData));
	return Exec.ActiveStates;
}


#if WITH_GAMEPLAY_DEBUGGER

FString FStateTreeExecutionContext::GetDebugInfoString(const FStateTreeInstanceData* ExternalInstanceData) const
{
	if (!StateTree)
	{
		return FString(TEXT("No StateTree asset."));
	}
	const FStateTreeInstanceData& InstanceData = SelectInstanceData(ExternalInstanceData);
	checkSlow(InstanceData.GetLayout() == StateTree->GetInstanceDataDefaultValue().GetLayout());
	const FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	FString DebugString = FString::Printf(TEXT("StateTree (asset: '%s')\n"), *GetNameSafe(StateTree));

	DebugString += TEXT("Status: ");
	switch (Exec.TreeRunStatus)
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
	DebugString += TEXT("Current State:\n");
	for (int32 Index = 0; Index < Exec.ActiveStates.Num(); Index++)
	{
		FStateTreeHandle Handle = Exec.ActiveStates[Index];
		if (Handle.IsValid())
		{
			const FCompactStateTreeState& State = StateTree->States[Handle.Index];
			DebugString += FString::Printf(TEXT("[%s]\n"), *State.Name.ToString());

			if (State.TasksNum > 0)
			{
				DebugString += TEXT("\nTasks:\n");
				for (int32 j = 0; j < int32(State.TasksNum); j++)
				{
					const FStateTreeTaskBase& Task = GetNode<FStateTreeTaskBase>(int32(State.TasksBegin) + j);
					Task.AppendDebugInfoString(DebugString, *this);
				}
			}
			if (State.EvaluatorsNum > 0)
			{
				DebugString += TEXT("\nEvaluators:\n");
				for (int32 EvalIndex = 0; EvalIndex < int32(State.EvaluatorsNum); EvalIndex++)
				{
					const FStateTreeEvaluatorBase& Eval = GetNode<FStateTreeEvaluatorBase>(int32(State.EvaluatorsBegin) + EvalIndex);
					Eval.AppendDebugInfoString(DebugString, *this);
				}
			}
		}
	}

	return DebugString;
}
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_STATETREE_DEBUG
void FStateTreeExecutionContext::DebugPrintInternalLayout(const FStateTreeInstanceData* ExternalInstanceData)
{
	LOG_SCOPE_VERBOSITY_OVERRIDE(LogStateTree, ELogVerbosity::Log);

	if (StateTree == nullptr)
	{
		UE_LOG(LogStateTree, Log, TEXT("No StateTree asset."));
		return;
	}

	FString DebugString = FString::Printf(TEXT("StateTree (asset: '%s')\n"), *GetNameSafe(StateTree));

	// Tree items (e.g. tasks, evaluators, conditions)
	DebugString += FString::Printf(TEXT("\nItems(%d)\n"), StateTree->Nodes.Num());
	for (const FInstancedStruct& Node : StateTree->Nodes)
	{
		DebugString += FString::Printf(TEXT("  %s\n"), Node.IsValid() ? *Node.GetScriptStruct()->GetName() : TEXT("null"));
	}

	// Instance InstanceData data (e.g. tasks, evaluators, conditions)
	DebugString += FString::Printf(TEXT("\nInstance Data(%d)\n"), StateTree->Instances.Num());
	for (const FInstancedStruct& Data : StateTree->Instances)
	{
		DebugString += FString::Printf(TEXT("  %s\n"), Data.IsValid() ? *Data.GetScriptStruct()->GetName() : TEXT("null"));
	}

	// Instance InstanceData offsets (e.g. tasks, evaluators, conditions)

	if (const FStateTreeInstanceDataLayout* InstanceLayout = StateTree->GetInstanceDataDefaultValue().GetLayout().Get())
	{
		DebugString += FString::Printf(TEXT("\nInstance Data Offsets(%d)\n  [ %-40s | %-6s ]\n"), InstanceLayout->Num(), TEXT("Name"), TEXT("Offset"));

		for (int32 Index = 0; Index < InstanceLayout->Num(); Index++)
		{
			const FStateTreeInstanceDataLayout::FLayoutItem& Item = InstanceLayout->GetItem(Index);
			DebugString += FString::Printf(TEXT("  | %-40s | %6d |\n"), Item.ScriptStruct ? *Item.ScriptStruct->GetName() : TEXT("null"), Item.Offset);
		}
	}

	// External data (e.g. fragments, subsystems)
	DebugString += FString::Printf(TEXT("\nExternal Data(%d)\n  [ %-40s | %-8s | %5s ]\n"), StateTree->ExternalDataDescs.Num(), TEXT("Name"), TEXT("Optional"), TEXT("Index"));
	for (const FStateTreeExternalDataDesc& Desc : StateTree->ExternalDataDescs)
	{
		DebugString += FString::Printf(TEXT("  | %-40s | %8s | %5d |\n"), Desc.Struct ? *Desc.Struct->GetName() : TEXT("null"), *UEnum::GetValueAsString(Desc.Requirement), Desc.Handle.DataViewIndex);
	}

	// Bindings
	StateTree->PropertyBindings.DebugPrintInternalLayout(DebugString);

	// Transitions
	DebugString += FString::Printf(TEXT("\nTransitions(%d)\n  [ %-3s | %15s | %-40s | %-40s | %-8s ]\n"), StateTree->Transitions.Num()
		, TEXT("Idx"), TEXT("State"), TEXT("Transition Type"), TEXT("Transition Event"), TEXT("Num Cond"));
	for (const FCompactStateTransition& Transition : StateTree->Transitions)
	{
		DebugString += FString::Printf(TEXT("  | %3d | %15s | %-40s | %-40s | %8d |\n"),
									Transition.ConditionsBegin, *Transition.State.Describe(),
									*UEnum::GetValueAsString(Transition.Type),
									*UEnum::GetValueAsString(Transition.Event),
									Transition.ConditionsNum);
	}

	// DataViews
	DebugString += FString::Printf(TEXT("\nDataViews(%d)\n"), DataViews.Num());
	for (const FStateTreeDataView& DataView : DataViews)
	{
		DebugString += FString::Printf(TEXT("  [%s]\n"), DataView.IsValid() ? *DataView.GetStruct()->GetName() : TEXT("null"));
	}

	// States
	DebugString += FString::Printf(TEXT("\nStates(%d)\n"
		"  [ %-30s | %15s | %5s [%3s:%-3s[ | Begin Idx : %4s %4s %4s %4s | Num : %4s %4s %4s %4s | Transitions : %-16s %-40s %-16s %-40s ]\n"),
		StateTree->States.Num(),
		TEXT("Name"), TEXT("Parent"), TEXT("Child"), TEXT("Beg"), TEXT("End"),
		TEXT("Cond"), TEXT("Tr"), TEXT("Tsk"), TEXT("Evt"), TEXT("Cond"), TEXT("Tr"), TEXT("Tsk"), TEXT("Evt"),
		TEXT("Done State"), TEXT("Done Type"), TEXT("Failed State"), TEXT("Failed Type")
		);
	for (const FCompactStateTreeState& State : StateTree->States)
	{
		DebugString += FString::Printf(TEXT("  | %-30s | %15s | %5s [%3d:%-3d[ | %9s   %4d %4d %4d %4d | %3s   %4d %4d %4d %4d\n"),
									*State.Name.ToString(), *State.Parent.Describe(),
									TEXT(""), State.ChildrenBegin, State.ChildrenEnd,
									TEXT(""), State.EnterConditionsBegin, State.TransitionsBegin, State.TasksBegin, State.EvaluatorsBegin,
									TEXT(""), State.EnterConditionsNum, State.TransitionsNum, State.TasksNum, State.EvaluatorsNum);
	}

	DebugString += FString::Printf(TEXT("\nStates\n  [ %-30s | %-12s | %-30s | %15s | %10s ]\n"),
		TEXT("State"), TEXT("Type"), TEXT("Name"), TEXT("Bindings"), TEXT("Struct Idx"));
	for (const FCompactStateTreeState& State : StateTree->States)
	{
		// Evaluators
		if (State.EvaluatorsNum)
		{
			for (int32 EvalIndex = 0; EvalIndex < State.EvaluatorsNum; EvalIndex++)
			{
				const FStateTreeEvaluatorBase& Eval = GetNode<FStateTreeEvaluatorBase>(State.EvaluatorsBegin + EvalIndex);
				DebugString += FString::Printf(TEXT("  | %-30s | %-12s | %-30s | %15s | %10d |\n"), *State.Name.ToString(),
					TEXT("  Evaluator"), *Eval.Name.ToString(), *Eval.BindingsBatch.Describe(), Eval.DataViewIndex);
			}
		}

		// Tasks
		if (State.TasksNum)
		{
			for (int32 TaskIndex = 0; TaskIndex < State.TasksNum; TaskIndex++)
			{
				const FStateTreeTaskBase& Task = GetNode<FStateTreeTaskBase>(State.TasksBegin + TaskIndex);
				DebugString += FString::Printf(TEXT("  | %-30s | %-12s | %-30s | %15s | %10d |\n"), *State.Name.ToString(),
					TEXT("  Task"), *Task.Name.ToString(), *Task.BindingsBatch.Describe(), Task.DataViewIndex);
			}
		}
	}

	UE_LOG(LogStateTree, Log, TEXT("%s"), *DebugString);
}

int32 FStateTreeExecutionContext::GetStateChangeCount(const FStateTreeInstanceData* ExternalInstanceData) const
{
	const FStateTreeInstanceData& InstanceData = SelectInstanceData(ExternalInstanceData);
	checkSlow(InstanceData.GetLayout() == StateTree->GetInstanceDataDefaultValue().GetLayout());
	const FStateTreeExecutionState& Exec = GetExecState(InstanceData);
	return Exec.StateChangeCount;
}

#endif // WITH_STATETREE_DEBUG

FString FStateTreeExecutionContext::GetActiveStateName(const FStateTreeInstanceData* ExternalInstanceData) const
{
	if (!StateTree)
	{
		return FString(TEXT("<None>"));
	}
	
	const FStateTreeInstanceData& InstanceData = SelectInstanceData(ExternalInstanceData);
	checkSlow(InstanceData.GetLayout() == StateTree->GetInstanceDataDefaultValue().GetLayout());
	const FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	FString FullStateName;
	
	// Active States
	for (int32 Index = 0; Index < Exec.ActiveStates.Num(); Index++)
	{
		const FStateTreeHandle Handle = Exec.ActiveStates[Index];
		if (Handle.IsValid())
		{
			const FCompactStateTreeState& State = StateTree->States[Handle.Index];
			bool bIsLinked = false;
			if (Index > 0)
			{
				FullStateName += TEXT("\n");
				bIsLinked = Exec.ActiveStates[Index - 1] != State.Parent;
			}
			FullStateName += FString::Printf(TEXT("%*s-"), Index * 3, TEXT("")); // Indent
			FullStateName += *State.Name.ToString();
			if (bIsLinked)
			{
				FullStateName += TEXT(" >");
			}
		}
	}

	switch (Exec.TreeRunStatus)
	{
	case EStateTreeRunStatus::Failed:
		FullStateName += TEXT(" FAILED\n");
		break;
	case EStateTreeRunStatus::Succeeded:
		FullStateName += TEXT(" SUCCEEDED\n");
		break;
	case EStateTreeRunStatus::Running:
		// Empty
		break;
	default:
		FullStateName += TEXT("--\n");
	}

	return FullStateName;
}

TArray<FName> FStateTreeExecutionContext::GetActiveStateNames(const FStateTreeInstanceData* ExternalInstanceData) const
{
	TArray<FName> Result;

	if (!StateTree)
	{
		return Result;
	}
	
	const FStateTreeInstanceData& InstanceData = SelectInstanceData(ExternalInstanceData);
	checkSlow(InstanceData.GetLayout() == StateTree->GetInstanceDataDefaultValue().GetLayout());
	const FStateTreeExecutionState& Exec = GetExecState(InstanceData);

	// Active States
	for (int32 Index = 0; Index < Exec.ActiveStates.Num(); Index++)
	{
		const FStateTreeHandle Handle = Exec.ActiveStates[Index];
		if (Handle.IsValid())
		{
			const FCompactStateTreeState& State = StateTree->States[Handle.Index];
			Result.Add(State.Name);
		}
	}

	return Result;
}

#undef STATETREE_LOG
#undef STATETREE_CLOG
