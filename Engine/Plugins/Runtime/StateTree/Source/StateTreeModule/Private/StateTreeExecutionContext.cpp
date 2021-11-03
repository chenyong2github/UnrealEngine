// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeExecutionContext.h"
#include "StateTree.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConditionBase.h"
#include "CoreMinimal.h"
#include "Engine/World.h"
#include "VisualLogger/VisualLogger.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Algo/Reverse.h"
#include "Logging/LogScopedVerbosityOverride.h"

#define STATETREE_LOG(Verbosity, Format, ...) UE_VLOG_UELOG(GetOwner(), LogStateTree, Verbosity, TEXT("%s: ") Format, *GetInstanceDescription(), ##__VA_ARGS__)
#define STATETREE_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG_UELOG((Condition), GetOwner(), LogStateTree, Verbosity, TEXT("%s: ") Format, *GetInstanceDescription(), ##__VA_ARGS__)

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

	if (!InStateTree.IsValidStateTree())
	{
		STATETREE_LOG(Error, TEXT("%s: StateTree asset '%s' is not valid."), ANSI_TO_TCHAR(__FUNCTION__), *InStateTree.GetName());
		Reset();
		return false;
	}
	StateTree = &InStateTree;

	StorageType = InStorageType;
	if (StorageType == EStateTreeStorage::Internal)
	{
		// Duplicate runtime state.
		StorageInstance = StateTree->RuntimeStorageDefaultValue;
	}

	// Initialize struct view for all possible source items.
	ItemViews.SetNum(StateTree->GetLinkedItemCount());

	// Initialize array to keep track of which states have been updated.
	VisitedStates.Init(false, StateTree->States.Num());
	
	return true;
}

void FStateTreeExecutionContext::Start(FStateTreeItemView ExternalStorage)
{
	if (!Owner || !StateTree)
	{
		return;
	}
	FStateTreeItemView Storage = SelectMutableStorage(ExternalStorage);
	FStateTreeExecutionState& Exec = GetExecState(Storage);

	// Stop if still running 
	if (Exec.TreeRunStatus == EStateTreeRunStatus::Running)
	{
		const FStateTreeTransitionResult Transition(FStateTreeStateStatus(Exec.CurrentState, Exec.LastTickStatus), FStateTreeHandle::Succeeded);
		ExitState(Storage, Transition);
	}

	// Initialize to unset running state, tick will choose the first state.
	Exec.TreeRunStatus = EStateTreeRunStatus::Running;
	Exec.CurrentState = FStateTreeHandle::Invalid;
	Exec.LastTickStatus = EStateTreeRunStatus::Unset;
}

void FStateTreeExecutionContext::Stop(FStateTreeItemView ExternalStorage)
{
	if (!Owner || !StateTree)
	{
		return;
	}
	FStateTreeItemView Storage = SelectMutableStorage(ExternalStorage);
	FStateTreeExecutionState& Exec = GetExecState(Storage);

	// Stop if still running.
	if (Exec.TreeRunStatus == EStateTreeRunStatus::Running)
	{
		const FStateTreeTransitionResult Transition(FStateTreeStateStatus(Exec.CurrentState, Exec.LastTickStatus), FStateTreeHandle::Succeeded);
		ExitState(Storage, Transition);
	}
	
	Exec.TreeRunStatus = EStateTreeRunStatus::Succeeded;
	Exec.CurrentState = FStateTreeHandle::Succeeded;
	Exec.LastTickStatus = EStateTreeRunStatus::Unset;
}

EStateTreeRunStatus FStateTreeExecutionContext::Tick(const float DeltaTime, FStateTreeItemView ExternalStorage)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeCtxTick);

	if (!Owner || !StateTree)
	{
		return EStateTreeRunStatus::Failed;
	}
	FStateTreeItemView Storage = SelectMutableStorage(ExternalStorage);
	FStateTreeExecutionState& Exec = GetExecState(Storage);

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
	
	// The state selection is repeated up to MaxIteration time. This allows failed EnterState() to potentially find a new state immediately.
	// This helps event driven StateTrees to not require another event/tick to find a suitable state.
	static const int32 MaxIterations = 5;
	for (int32 Iter = 0; Iter < MaxIterations; Iter++)
	{
		// Reset visited state, used to keep track which state's evaluators have been ticked.
		VisitedStates.Init(false, StateTree->States.Num());

		// Tick evaluators on active states. Further evaluator may be ticked during selection as non-active states are visited.
		// Ticked states are kept track of and evaluators are ticked just once per frame.
		if (Exec.LastTickStatus == EStateTreeRunStatus::Running)
		{
			TickEvaluators(Storage, Exec.CurrentState, EStateTreeEvaluationType::Tick, DeltaTime);
		}

		// Select initial state or trigger transitions.
		FStateTreeTransitionResult Transition;
		if (Exec.LastTickStatus == EStateTreeRunStatus::Unset && Exec.CurrentState == FStateTreeHandle::Invalid)
		{
			// No state has been selected yet, select starting from root.
			static const FStateTreeHandle RootState = FStateTreeHandle(0);
			Transition = SelectState(Storage, FStateTreeStateStatus(FStateTreeHandle::Invalid), RootState, RootState, 0);
		}
		else
		{
			// Trigger running transitions or state succeed/failed transitions.
			// Transitions are triggered early in the frame, to prevent one frame lag in games which require quick state changes (i.e. combat).
			Transition = TriggerTransitions(Storage, FStateTreeStateStatus(Exec.CurrentState, Exec.LastTickStatus), 0);
		}

		// Handle potential transition.
		if (Transition.Next.IsValid())
		{
			ExitState(Storage, Transition);
			
			if (Transition.Next == FStateTreeHandle::Succeeded || Transition.Next == FStateTreeHandle::Failed)
			{
				// Transition to a terminal state (succeeded/failed), or default transition failed.
				Exec.TreeRunStatus = Transition.Next == FStateTreeHandle::Succeeded ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
				return Exec.TreeRunStatus;
			}
			
			// Enter state tasks can fail/succeed, treat it same as tick.
			Exec.LastTickStatus = EnterState(Storage, Transition);
			Exec.CurrentState = Transition.Next;
			// Report state completed immediately.
			if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
			{
				StateCompleted(Storage, Exec.CurrentState, Exec.LastTickStatus);
			}
		}
		
		// Stop as soon as have found a running state.
		if (Exec.LastTickStatus == EStateTreeRunStatus::Running)
		{
			break;
		}
	}
	
	if (!Exec.CurrentState.IsValid())
	{
		// Should not happen. This may happen if initial state or default transition could not be selected. 
		STATETREE_LOG(Error, TEXT("%s: Failed to select state on '%s' using StateTree '%s'."), ANSI_TO_TCHAR(__FUNCTION__), *GetNameSafe(Owner), *GetNameSafe(StateTree));
		Exec.TreeRunStatus = EStateTreeRunStatus::Failed;
		return Exec.TreeRunStatus;
	}

	// Tick tasks on active states.
	if (Exec.LastTickStatus == EStateTreeRunStatus::Running)
	{
		Exec.LastTickStatus = TickTasks(Storage, Exec.CurrentState, DeltaTime);
		// Report state completed immediately.
		if (Exec.LastTickStatus != EStateTreeRunStatus::Running)
		{
			StateCompleted(Storage, Exec.CurrentState, Exec.LastTickStatus);
		}
	}
	
	
	return Exec.TreeRunStatus;
}

void FStateTreeExecutionContext::Reset()
{
	StateTree = nullptr;
	Owner = nullptr;
	World = nullptr;
	ItemViews.Reset();
	StorageInstance.Reset();
	StorageType = EStateTreeStorage::Internal;
	VisitedStates.Reset();
}

EStateTreeRunStatus FStateTreeExecutionContext::EnterState(FStateTreeItemView Storage, const FStateTreeTransitionResult& Transition)
{
	if (!Transition.Next.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}

	// Activate evaluators along the active branch.
	TStaticArray<FStateTreeHandle, 32> States;
	const int32 NumStates = GetActiveStates(Transition.Next, States);
	TStaticArray<FStateTreeHandle, 32> PrevStates;
	const int32 NumPrevStates = GetActiveStates(Transition.Source.State, PrevStates);
	// Pad prev states with invalid states if it's shorter.
	for (int32 Index = NumPrevStates; Index < NumStates; Index++)
	{
		PrevStates[Index] = FStateTreeHandle::Invalid;
	}

	// On target branch means that the state is the target of current transition or child of it.
	// States which were active before and will remain active, but are not on target branch will not get
	// EnterState called. That is, a transition is handled as "replan from this state".
	bool bOnTargetBranch = false;

	FStateTreeTransitionResult CurrentTransition = Transition;
	
	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;
	EnterStateStatus = Result;

	for (int32 Index = 0; Index < NumStates; Index++)
	{
		const FStateTreeHandle CurrentHandle = States[Index];
		const FBakedStateTreeState& State = StateTree->States[CurrentHandle.Index];
		bOnTargetBranch = bOnTargetBranch || CurrentHandle == Transition.Target;
		const bool bWasActive = PrevStates[Index] == CurrentHandle;
		if (bWasActive && !bOnTargetBranch)
		{
			// States which will keep on begin active and were not part of the transition will not get enter/exit state.
			// Must update item views.
			for (int32 EvalIndex = 0; EvalIndex < int32(State.EvaluatorsNum); EvalIndex++)
			{
				FStateTreeItemView EvalView = GetItem(Storage, int32(State.EvaluatorsBegin) + EvalIndex);
				const FStateTreeEvaluatorBase& Eval = EvalView.Get<FStateTreeEvaluatorBase>();
				ItemViews[Eval.SourceStructIndex] = EvalView;
			}
			for (int32 TaskIndex = 0; TaskIndex < int32(State.TasksNum); TaskIndex++)
			{
				FStateTreeItemView TaskView = GetItem(Storage, int32(State.TasksBegin) + TaskIndex);
				const FStateTreeTaskBase& Task = TaskView.Get<FStateTreeTaskBase>();
				ItemViews[Task.SourceStructIndex] = TaskView;
			}
			continue;
		}

		CurrentTransition.Current = CurrentHandle;
		
		const EStateTreeStateChangeType ChangeType = bWasActive ? EStateTreeStateChangeType::Sustained : EStateTreeStateChangeType::Changed;

		STATETREE_LOG(Log, TEXT("%*sEnter state '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(States, Index), *StaticEnum<EStateTreeStateChangeType>()->GetValueAsString(ChangeType));
		
		for (int32 EvalIndex = 0; EvalIndex < int32(State.EvaluatorsNum); EvalIndex++)
		{
			FStateTreeItemView EvalView = GetItem(Storage, int32(State.EvaluatorsBegin) + EvalIndex);
			FStateTreeEvaluatorBase& Eval = EvalView.GetMutable<FStateTreeEvaluatorBase>();

			// Copy bound properties.
			if (Eval.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(ItemViews, Eval.BindingsBatch.Index, EvalView);
			}
			STATETREE_LOG(Verbose, TEXT("%*s  Notify Evaluator '%s'."), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Eval.Name.ToString());
			Eval.EnterState(*this, ChangeType, CurrentTransition);

			ItemViews[Eval.SourceStructIndex] = EvalView;
		}
		
		// Activate tasks on current state.
		for (int32 TaskIndex = 0; TaskIndex < int32(State.TasksNum); TaskIndex++)
		{
			FStateTreeItemView TaskView = GetItem(Storage, int32(State.TasksBegin) + TaskIndex);
			FStateTreeTaskBase& Task = TaskView.GetMutable<FStateTreeTaskBase>();

			// Copy bound properties.
			if (Task.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(ItemViews, Task.BindingsBatch.Index, TaskView);
			}
		
			STATETREE_LOG(Verbose, TEXT("%*s  Notify Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
			const EStateTreeRunStatus Status = Task.EnterState(*this, ChangeType, CurrentTransition);
			if (Status == EStateTreeRunStatus::Failed)
			{
				Result = Status;
				// @todo StateTree: Ideally we should break here, but it is commented out for now in order to keep symmetrical enter/exit calls.
				// break;
				// As a workaround, we mark EnterStateStatus as 'failed' in context so remaining calls to EnterState could be aware of the failure and act accordingly.
				EnterStateStatus = Status;

			}

			ItemViews[Task.SourceStructIndex] = TaskView;
		}
	}

	return Result;
}

void FStateTreeExecutionContext::ExitState(FStateTreeItemView Storage, const FStateTreeTransitionResult& Transition)
{
	if (!Transition.Source.State.IsValid())
	{
		return;
	}

	// Reset transition delay
	FStateTreeExecutionState& Exec = GetExecState(Storage);
	Exec.GatedTransitionIndex = INDEX_NONE;
	Exec.GatedTransitionTime = 0.0f;

	// Deactivate evaluators along the active branch.
	TStaticArray<FStateTreeHandle, 32> States;
	const int32 NumStates = GetActiveStates(Transition.Source.State, States);
	TStaticArray<FStateTreeHandle, 32> NextStates;
	const int32 NumNextStates = GetActiveStates(Transition.Next, NextStates);
	// Pad next states with invalid states if it's shorter.
	for (int32 Index = NumNextStates; Index < NumStates; Index++)
	{
		NextStates[Index] = FStateTreeHandle::Invalid;
	}

	// On target branch means that the state is the target of current transition or child of it.
	// States which were active before and will remain active, but are not on target branch will not get
	// EnterState called. That is, a transition is handled as "replan from this state".
	bool bOnTargetBranch = false;

	FStateTreeTransitionResult CurrentTransition = Transition;

	// It would be more symmetrical, if the evals, tasks and states were executed in reverse order, but we need
	// to do it like this because of the property copies.
	for (int32 Index = 0; Index < NumStates; Index++)
	{
		const FStateTreeHandle CurrentHandle = States[Index];
		const FStateTreeHandle NextHandle = NextStates[Index];
		const FBakedStateTreeState& State = StateTree->States[CurrentHandle.Index];
		const bool bRemainsActive = NextHandle == States[Index];
		bOnTargetBranch = bOnTargetBranch || NextHandle == Transition.Target;
		if (bRemainsActive && !bOnTargetBranch)
		{
			// States which will keep on begin active and were not part of the transition will not get enter/exit state.
			// Must update item views.
			for (int32 EvalIndex = 0; EvalIndex < int32(State.EvaluatorsNum); EvalIndex++)
			{
				FStateTreeItemView EvalView = GetItem(Storage, int32(State.EvaluatorsBegin) + EvalIndex);
				const FStateTreeEvaluatorBase& Eval = EvalView.Get<FStateTreeEvaluatorBase>();
				ItemViews[Eval.SourceStructIndex] = EvalView;
			}
			for (int32 TaskIndex = 0; TaskIndex < int32(State.TasksNum); TaskIndex++)
			{
				FStateTreeItemView TaskView = GetItem(Storage, int32(State.TasksBegin) + TaskIndex);
				const FStateTreeTaskBase& Task = TaskView.Get<FStateTreeTaskBase>();
				ItemViews[Task.SourceStructIndex] = TaskView;
			}
			continue;
		}

		CurrentTransition.Current = CurrentHandle;
		
		const EStateTreeStateChangeType ChangeType = bRemainsActive ? EStateTreeStateChangeType::Sustained : EStateTreeStateChangeType::Changed;

		STATETREE_LOG(Log, TEXT("%*sExit state '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(States, Index), *StaticEnum<EStateTreeStateChangeType>()->GetValueAsString(ChangeType));

		for (int32 EvalIndex = 0; EvalIndex < int32(State.EvaluatorsNum); EvalIndex++)
		{
			FStateTreeItemView EvalView = GetItem(Storage, int32(State.EvaluatorsBegin) + EvalIndex);
			FStateTreeEvaluatorBase& Eval = EvalView.GetMutable<FStateTreeEvaluatorBase>();

			// Copy bound properties.
			if (Eval.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(ItemViews, Eval.BindingsBatch.Index, EvalView);
			}
			STATETREE_LOG(Verbose, TEXT("%*s  Notify Evaluator '%s'."), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Eval.Name.ToString());
			Eval.ExitState(*this, ChangeType, CurrentTransition);

			ItemViews[Eval.SourceStructIndex] = EvalView;
		}

		// Deactivate tasks on current State
		for (int32 TaskIndex = 0; TaskIndex < int32(State.TasksNum); TaskIndex++)
		{
			FStateTreeItemView TaskView = GetItem(Storage, int32(State.TasksBegin) + TaskIndex);
			FStateTreeTaskBase& Task = TaskView.GetMutable<FStateTreeTaskBase>();

			// Copy bound properties.
			if (Task.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(ItemViews, Task.BindingsBatch.Index, TaskView);
			}

			STATETREE_LOG(Verbose, TEXT("%*s  Notify Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
			Task.ExitState(*this, ChangeType, CurrentTransition);

			ItemViews[Task.SourceStructIndex] = TaskView;
		}
	}
}

void FStateTreeExecutionContext::StateCompleted(FStateTreeItemView Storage, const FStateTreeHandle CurrentState, const EStateTreeRunStatus CompletionStatus)
{
	if (!CurrentState.IsValid())
	{
		return;
	}

	TStaticArray<FStateTreeHandle, 32> States;
	const int32 NumStates = GetActiveStates(CurrentState, States);

	// Call from child towards root to allow to pass results back.
	// Note: Completed is assumed to be called immediately after tick or enter state, so there's no property copying.
	for (int32 Index = NumStates - 1; Index >= 0; Index--)
	{
		const FStateTreeHandle CurrentHandle = States[Index];
		const FBakedStateTreeState& State = StateTree->States[CurrentHandle.Index];

		STATETREE_LOG(Verbose, TEXT("%*sState Completed '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(States, Index), *StaticEnum<EStateTreeRunStatus>()->GetValueAsString(CompletionStatus));

		// Notify Tasks
		for (int32 TaskIndex = int32(State.TasksNum) - 1; TaskIndex >= 0; TaskIndex--)
		{
			FStateTreeItemView TaskView = GetItem(Storage, int32(State.TasksBegin) + TaskIndex);
			FStateTreeTaskBase& Task = TaskView.GetMutable<FStateTreeTaskBase>();

			STATETREE_LOG(Verbose, TEXT("%*s  Notify Task '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
			Task.StateCompleted(*this, CompletionStatus, CurrentState);
		}

		// Notify evaluators
		for (int32 EvalIndex = int32(State.EvaluatorsNum) - 1; EvalIndex >= 0; EvalIndex--)
		{
			FStateTreeItemView EvalView = GetItem(Storage, int32(State.EvaluatorsBegin) + EvalIndex);
			FStateTreeEvaluatorBase& Eval = EvalView.GetMutable<FStateTreeEvaluatorBase>();

			STATETREE_LOG(Verbose, TEXT("%*s  Notify Evaluator '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Eval.Name.ToString());
			Eval.StateCompleted(*this, CompletionStatus, CurrentState);
		}
	}
}

void FStateTreeExecutionContext::TickEvaluators(FStateTreeItemView Storage, const FStateTreeHandle CurrentState, const EStateTreeEvaluationType EvalType, const float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeCtxTickEvaluators);

	if (!CurrentState.IsValid())
	{
		return;
	}

	TStaticArray<FStateTreeHandle, 32> States;
	const int32 NumStates = GetActiveStates(CurrentState, States);

	for (int32 Index = 0; Index < NumStates; Index++)
	{
		const FStateTreeHandle CurrentHandle = States[Index];
		if (VisitedStates[CurrentHandle.Index])
		{
			// Already ticked this frame.
			continue;
		}

		const FBakedStateTreeState& State = StateTree->States[CurrentHandle.Index];

		STATETREE_CLOG(State.EvaluatorsNum > 0, Verbose, TEXT("%*sTicking Evaluators of state '%s' %s"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(States, Index), *StaticEnum<EStateTreeEvaluationType>()->GetValueAsString(EvalType));

		// Tick evaluators
		for (int32 EvalIndex = 0; EvalIndex < int32(State.EvaluatorsNum); EvalIndex++)
		{
			FStateTreeItemView EvalView = GetItem(Storage, int32(State.EvaluatorsBegin) + EvalIndex);
			FStateTreeEvaluatorBase& Eval = EvalView.GetMutable<FStateTreeEvaluatorBase>();

			// Copy bound properties.
			if (Eval.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(ItemViews, Eval.BindingsBatch.Index, EvalView);
			}
			STATETREE_LOG(Verbose, TEXT("%*s  Evaluate: '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Eval.Name.ToString());
			Eval.Evaluate(*this, EvalType, DeltaTime);
			ItemViews[Eval.SourceStructIndex] = EvalView;
		}

		VisitedStates[CurrentHandle.Index] = true;
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::TickTasks(FStateTreeItemView Storage, const FStateTreeHandle CurrentState, const float DeltaTime)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeCtxTickTasks);

	if (!CurrentState.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}

	TStaticArray<FStateTreeHandle, 32> States;
	const int32 NumStates = GetActiveStates(CurrentState, States);

	EStateTreeRunStatus Result = EStateTreeRunStatus::Running;
	int32 NumTotalTasks = 0;

	for (int32 Index = 0; Index < NumStates && Result != EStateTreeRunStatus::Failed; Index++)
	{
		const FStateTreeHandle CurrentHandle = States[Index];
		const FBakedStateTreeState& State = StateTree->States[CurrentHandle.Index];

		STATETREE_CLOG(State.TasksNum > 0, Verbose, TEXT("%*sTicking Tasks of state '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *DebugGetStatePath(States, Index));

		// Tick Tasks
		for (int32 TaskIndex = 0; TaskIndex < int32(State.TasksNum); TaskIndex++)
		{
			FStateTreeItemView TaskView = GetItem(Storage, int32(State.TasksBegin) + TaskIndex);
			FStateTreeTaskBase& Task = TaskView.GetMutable<FStateTreeTaskBase>();

			// Copy bound properties.
			if (Task.BindingsBatch.IsValid())
			{
				StateTree->PropertyBindings.CopyTo(ItemViews, Task.BindingsBatch.Index, TaskView);
			}
			STATETREE_LOG(Verbose, TEXT("%*s  Tick: '%s'"), Index*UE::StateTree::DebugIndentSize, TEXT(""), *Task.Name.ToString());
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

			ItemViews[Task.SourceStructIndex] = TaskView;
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
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(StateTreeCtxTestConditions);

	uint8 CondCopyBuffer[128];	// This buffer is intentionally small, conditions be small.
	
	for (uint32 i = 0; i < ConditionsNum; i++)
	{
		// Copy struct values from the StateTree asset to a stack copy.
		const FInstancedStruct& SourceCondPtr = StateTree->Conditions[ConditionsOffset + i];
		const FStateTreeConditionBase& SourceCond = SourceCondPtr.Get<FStateTreeConditionBase>();
		const UScriptStruct* CondStruct = SourceCondPtr.GetScriptStruct();
		check(CondStruct);
		check(CondStruct->GetStructureSize() <= sizeof(CondCopyBuffer));

		CondStruct->InitializeStruct(CondCopyBuffer);
		CondStruct->CopyScriptStruct(CondCopyBuffer, &SourceCond);

		// Copy properties over
		FStateTreeItemView CondCopyView(CondStruct, CondCopyBuffer);
		if (SourceCond.BindingsBatch.IsValid())
		{
			StateTree->PropertyBindings.CopyTo(ItemViews, SourceCond.BindingsBatch.Index, CondCopyView);
		}

		// Do test (virtual call)
		const FStateTreeConditionBase& Cond = CondCopyView.Get<FStateTreeConditionBase>();
		const bool bResult = Cond.TestCondition();

		// Struct destructor
		CondStruct->DestroyStruct(CondCopyBuffer);

		if (!bResult)
		{
			return false;
		}
	}
	return true;
}

FStateTreeTransitionResult FStateTreeExecutionContext::TriggerTransitions(FStateTreeItemView Storage, const FStateTreeStateStatus CurrentStatus, const int32 Depth)
{
	FStateTreeExecutionState& Exec = GetExecState(Storage);
	EStateTreeTransitionEvent Event = EStateTreeTransitionEvent::OnCondition;
	if (CurrentStatus.RunStatus == EStateTreeRunStatus::Succeeded)
	{
		Event = EStateTreeTransitionEvent::OnSucceeded;
	}
	else if (CurrentStatus.RunStatus == EStateTreeRunStatus::Failed)
	{
		Event = EStateTreeTransitionEvent::OnFailed;
	}

	// Walk towards root and check all transitions along the way.
	for (FStateTreeHandle Handle = CurrentStatus.State; Handle.IsValid(); Handle = StateTree->States[Handle.Index].Parent)
	{
		const FBakedStateTreeState& State = StateTree->States[Handle.Index];
		for (uint32 i = 0; i < State.TransitionsNum; i++)
		{
			// All transition conditions must pass
			const int16 TransitionIndex = State.TransitionsBegin + i;
			const FBakedStateTransition& Transition = StateTree->Transitions[TransitionIndex];
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
						STATETREE_LOG(Verbose, TEXT("Gated transition triggered from '%s' (%s) -> '%s' %.1fs"), *GetSafeStateName(CurrentStatus.State), *State.Name.ToString(), *GetSafeStateName(Transition.State), Exec.GatedTransitionTime);
					}

					// Keep on updating current state, until we have tried to trigger
					if (Exec.GatedTransitionTime > 0.0f)
					{
						return FStateTreeTransitionResult(CurrentStatus, FStateTreeHandle::Invalid);
					}

					STATETREE_LOG(Verbose, TEXT("Passed gated transition from '%s' (%s) -> '%s'"), *GetSafeStateName(CurrentStatus.State), *State.Name.ToString(), *GetSafeStateName(Transition.State));
				}
				
				if (Transition.Type == EStateTreeTransitionType::GotoState || Transition.Type == EStateTreeTransitionType::NextState)
				{
					FStateTreeTransitionResult Result = SelectState(Storage, CurrentStatus, Transition.State, Transition.State, Depth + 1);
					if (Result.Next.IsValid())
					{
						STATETREE_LOG(Verbose, TEXT("Transition on state '%s' (%s) -[%s]-> state '%s'"), *GetSafeStateName(CurrentStatus.State), *State.Name.ToString(), *GetSafeStateName(Result.Target), *GetSafeStateName(Result.Next));
						return Result;
					}
				}
				else if (Transition.Type == EStateTreeTransitionType::NotSet)
				{
					// NotSet is no-operation, but can be used to mask a transition at parent state. Returning unset keeps updating current state.
					return FStateTreeTransitionResult(CurrentStatus, FStateTreeHandle::Invalid);
				}
				else if (Transition.Type == EStateTreeTransitionType::Succeeded)
				{
					STATETREE_LOG(Verbose, TEXT("Stop tree execution from state '%s' (%s): Succeeded"), *GetSafeStateName(CurrentStatus.State), *State.Name.ToString());
					return FStateTreeTransitionResult(CurrentStatus, FStateTreeHandle::Succeeded);
				}
				else
				{
					STATETREE_LOG(Verbose, TEXT("Stop tree execution from state '%s' (%s): Failed"), *GetSafeStateName(CurrentStatus.State), *State.Name.ToString());
					return FStateTreeTransitionResult(CurrentStatus, FStateTreeHandle::Failed);
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

	if (CurrentStatus.RunStatus != EStateTreeRunStatus::Running)
	{
		STATETREE_LOG(Error, TEXT("%s: Default transitions on state '%s' could not be triggered. '%s' using StateTree '%s'."),
            ANSI_TO_TCHAR(__FUNCTION__), *GetSafeStateName(CurrentStatus.State), *GetNameSafe(Owner), *GetNameSafe(StateTree));
		return FStateTreeTransitionResult(CurrentStatus, FStateTreeHandle::Failed);
	}

	// No transition triggered, keep on updating current state.
	return FStateTreeTransitionResult(CurrentStatus, FStateTreeHandle::Invalid);
}

FStateTreeTransitionResult FStateTreeExecutionContext::SelectState(FStateTreeItemView Storage, const FStateTreeStateStatus InitialStateStatus, const FStateTreeHandle InitialTargetState, const FStateTreeHandle NextState, const int Depth)
{
	if (!NextState.IsValid())
	{
		// Trying to select non-existing state.
		STATETREE_LOG(Error, TEXT("%s: Trying to select invalid state from '%s' via '%s.  '%s' using StateTree '%s'."),
            ANSI_TO_TCHAR(__FUNCTION__), *GetStateStatusString(InitialStateStatus), *GetSafeStateName(InitialTargetState), *GetNameSafe(Owner), *GetNameSafe(StateTree));
		return FStateTreeTransitionResult(InitialStateStatus, InitialTargetState, FStateTreeHandle::Invalid);
	}

	const FBakedStateTreeState& State = StateTree->States[NextState.Index];

	// Make sure all the evaluators for the target state are up to date.
	TickEvaluators(Storage, NextState, EStateTreeEvaluationType::PreSelect, 0.0f);
	
	// Check that the state can be entered
	if (TestAllConditions(State.EnterConditionsBegin, State.EnterConditionsNum))
	{
		// If the state has children, proceed to select children.
		if (State.HasChildren())
		{
			for (uint16 ChildState = State.ChildrenBegin; ChildState < State.ChildrenEnd; ChildState = StateTree->States[ChildState].GetNextSibling())
			{
				const FStateTreeTransitionResult ChildResult = SelectState(Storage, InitialStateStatus, InitialTargetState, FStateTreeHandle(ChildState), Depth + 1);
				if (ChildResult.Next.IsValid())
				{
					// Selection succeeded
					return ChildResult;
				}
			}
		}
		else
		{
			// Select this state.
			return FStateTreeTransitionResult(InitialStateStatus, InitialTargetState, NextState);
		}
	}
	
	// Nothing got selected.
	return FStateTreeTransitionResult(InitialStateStatus, InitialTargetState, FStateTreeHandle::Invalid);
}

int32 FStateTreeExecutionContext::GetActiveStates(const FStateTreeHandle StateHandle, TStaticArray<FStateTreeHandle, 32>& OutStateHandles) const
{
	if (StateHandle == FStateTreeHandle::Succeeded || StateHandle == FStateTreeHandle::Failed || StateHandle == FStateTreeHandle::Invalid)
	{
		return 0;
	}
	
	int32 NumStates = 0;
	FStateTreeHandle CurrentHandle = StateHandle;
	while (CurrentHandle.IsValid() && NumStates < OutStateHandles.Num())
	{
		OutStateHandles[NumStates++] = CurrentHandle;
		CurrentHandle = StateTree->States[CurrentHandle.Index].Parent;
	}

	Algo::Reverse(OutStateHandles.GetData(), NumStates);
	
	return NumStates;
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

FString FStateTreeExecutionContext::DebugGetStatePath(const TArrayView<FStateTreeHandle> ActiveStateHandles, const int32 ActiveStateIndex) const
{
	FString StatePath;
	if (!ensureMsgf(ActiveStateHandles.IsValidIndex(ActiveStateIndex), TEXT("Provided index must be valid")))
	{
		return StatePath;
	}

	for (int32 i = 0; i <= ActiveStateIndex; i++)
	{
        const FBakedStateTreeState& State = StateTree->States[ActiveStateHandles[i].Index];
		StatePath.Appendf(TEXT("%s%s"), i == 0 ? TEXT("") : TEXT("."), *State.Name.ToString());
	}
	return StatePath;
}

FString FStateTreeExecutionContext::GetStateStatusString(const FStateTreeStateStatus StateStatus) const
{
	// Invalid (null) state in status refers to the whole tree.
	const FString StateName = GetSafeStateName(StateStatus.State);
	switch(StateStatus.RunStatus)
	{
	case EStateTreeRunStatus::Unset:
		return StateName + TEXT("Unset");
	case EStateTreeRunStatus::Running:
		return StateName + TEXT("Running");
	case EStateTreeRunStatus::Succeeded:
		return StateName + TEXT("Succeeded");
	case EStateTreeRunStatus::Failed:
		return StateName + TEXT("Failed");
	default:
		return StateName + TEXT("(Unknown)");
	}
}

EStateTreeRunStatus FStateTreeExecutionContext::GetLastTickStatus(FStateTreeItemView ExternalStorage) const
{
	const FStateTreeExecutionState& Exec = GetExecState(SelectStorage(ExternalStorage));
	return Exec.LastTickStatus;
}

#if WITH_GAMEPLAY_DEBUGGER

FString FStateTreeExecutionContext::GetDebugInfoString(FStateTreeItemView ExternalStorage) const
{
	if (!StateTree)
	{
		return FString(TEXT("No StateTree asset."));
	}
	FStateTreeItemView Storage = SelectStorage(ExternalStorage);
	FStateTreeExecutionState& Exec = GetExecState(Storage);

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
	TStaticArray<FStateTreeHandle, 32> ActiveStates;
	const int32 NumStates = GetActiveStates(Exec.CurrentState, ActiveStates);

	DebugString += TEXT("Current State:\n");
	for (int32 Index = 0; Index < NumStates; Index++)
	{
		FStateTreeHandle Handle = ActiveStates[Index];
		if (Handle.IsValid())
		{
			const FBakedStateTreeState& State = StateTree->States[Handle.Index];
			DebugString += FString::Printf(TEXT("[%s]\n"), *State.Name.ToString());

			if (State.TasksNum > 0)
			{
				DebugString += TEXT("\nTasks:\n");
				for (int32 j = 0; j < int32(State.TasksNum); j++)
				{
					FStateTreeItemView TaskView = GetItem(Storage, int32(State.TasksBegin) + j);
					const FStateTreeTaskBase& Task = TaskView.Get<FStateTreeTaskBase>();
					Task.AppendDebugInfoString(DebugString, *this);
				}
			}
			if (State.EvaluatorsNum > 0)
			{
				DebugString += TEXT("\nEvaluators:\n");
				for (int32 EvalIndex = 0; EvalIndex < int32(State.EvaluatorsNum); EvalIndex++)
				{
					FStateTreeItemView EvalView = GetItem(Storage, int32(State.EvaluatorsBegin) + EvalIndex);
					const FStateTreeEvaluatorBase& Eval = EvalView.Get<FStateTreeEvaluatorBase>();
					Eval.AppendDebugInfoString(DebugString, *this);
				}
			}
		}
	}

	return DebugString;
}
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_STATETREE_DEBUG
void FStateTreeExecutionContext::DebugPrintInternalLayout(FStateTreeItemView ExternalStorage)
{
	LOG_SCOPE_VERBOSITY_OVERRIDE(LogStateTree, ELogVerbosity::Log);

	if (StateTree == nullptr)
	{
		UE_LOG(LogStateTree, Log, TEXT("No StateTree asset."));
		return;
	}

	FString DebugString = FString::Printf(TEXT("StateTree (asset: '%s')\n"), *GetNameSafe(StateTree));

	// Runtime storage items (e.g. tasks, evaluators)
	DebugString += FString::Printf(TEXT("\nItems(%d)\n"), StateTree->RuntimeStorageItems.Num());
	for (const FInstancedStruct& Item : StateTree->RuntimeStorageItems)
	{
		DebugString += FString::Printf(TEXT("  %s\n"), Item.IsValid() ? *Item.GetScriptStruct()->GetName() : TEXT("null"));
	}

	// Runtime storage offsets (e.g. tasks, evaluators)
	DebugString += FString::Printf(TEXT("\nOffsets(%d)\n  [ %-40s | %-6s ]\n"), StateTree->RuntimeStorageOffsets.Num(), TEXT("Name"), TEXT("Offset"));
	for (const FStateTreeRuntimeStorageItemOffset& Offset : StateTree->RuntimeStorageOffsets)
	{
		DebugString += FString::Printf(TEXT("  | %-40s | %6d |\n"), Offset.Struct ? *Offset.Struct->GetName() : TEXT("null"),  Offset.Offset);
	}

	// Conditions
	DebugString += FString::Printf(TEXT("\nConditions(%d)\n"), StateTree->Conditions.Num());
	for (const FInstancedStruct& Condition : StateTree->Conditions)
	{
		DebugString += FString::Printf(TEXT("  %s\n"), Condition.IsValid() ? *Condition.GetScriptStruct()->GetName() : TEXT("null"));
	}

	// External items (e.g. fragments, subsystems)
	DebugString += FString::Printf(TEXT("\nExternal items(%d)\n  [ %-40s | %-8s | %5s ]\n"), StateTree->ExternalItems.Num(), TEXT("Name"), TEXT("Optional"), TEXT("Index"));
	for (const FStateTreeExternalItemDesc& Desc : StateTree->ExternalItems)
	{
		DebugString += FString::Printf(TEXT("  | %-40s | %8s | %5d |\n"), Desc.Struct ? *Desc.Struct->GetName() : TEXT("null"), *UEnum::GetValueAsString(Desc.Requirement), Desc.Handle.ItemIndex);
	}

	// Bindings
	StateTree->PropertyBindings.DebugPrintInternalLayout(DebugString);

	// Transitions
	DebugString += FString::Printf(TEXT("\nTransitions(%d)\n  [ %-3s | %15s | %-40s | %-40s | %-8s ]\n"), StateTree->Transitions.Num()
		, TEXT("Idx"), TEXT("State"), TEXT("Transition Type"), TEXT("Transition Event"), TEXT("Num Cond"));
	for (const FBakedStateTransition& Transition : StateTree->Transitions)
	{
		DebugString += FString::Printf(TEXT("  | %3d | %15s | %-40s | %-40s | %8d |\n"),
									Transition.ConditionsBegin, *Transition.State.Describe(),
									*UEnum::GetValueAsString(Transition.Type),
									*UEnum::GetValueAsString(Transition.Event),
									Transition.ConditionsNum);
	}

	// ItemViews
	DebugString += FString::Printf(TEXT("\nItemViews(%d)\n"), ItemViews.Num());
	for (const FStateTreeItemView& ItemView : ItemViews)
	{
		DebugString += FString::Printf(TEXT("  [%s]\n"), ItemView.IsValid() ? *ItemView.GetStruct()->GetName() : TEXT("null"));
	}

	// States
	FStateTreeItemView Storage = SelectMutableStorage(ExternalStorage);
	DebugString += FString::Printf(TEXT("\nStates(%d)\n"
		"  [ %-30s | %15s | %5s [%3s:%-3s[ | Begin Idx : %4s %4s %4s %4s | Num : %4s %4s %4s %4s | Transitions : %-16s %-40s %-16s %-40s ]\n"),
		StateTree->States.Num(),
		TEXT("Name"), TEXT("Parent"), TEXT("Child"), TEXT("Beg"), TEXT("End"),
		TEXT("Cond"), TEXT("Tr"), TEXT("Tsk"), TEXT("Evt"), TEXT("Cond"), TEXT("Tr"), TEXT("Tsk"), TEXT("Evt"),
		TEXT("Done State"), TEXT("Done Type"), TEXT("Failed State"), TEXT("Failed Type")
		);
	for (const FBakedStateTreeState& State : StateTree->States)
	{
		DebugString += FString::Printf(TEXT("  | %-30s | %15s | %5s [%3d:%-3d[ | %9s   %4d %4d %4d %4d | %3s   %4d %4d %4d %4d | %11s   %-16s %-40s %-16s %-40s |\n"),
									*State.Name.ToString(), *State.Parent.Describe(),
									TEXT(""), State.ChildrenBegin, State.ChildrenEnd,
									TEXT(""), State.EnterConditionsBegin, State.TransitionsBegin, State.TasksBegin, State.EvaluatorsBegin,
									TEXT(""), State.EnterConditionsNum, State.TransitionsNum, State.TasksNum, State.EvaluatorsNum,
									TEXT(""), *State.StateDoneTransitionState.Describe(), *StaticEnum<EStateTreeTransitionType>()->GetValueAsString(State.StateDoneTransitionType),
									*State.StateFailedTransitionState.Describe(), *StaticEnum<EStateTreeTransitionType>()->GetValueAsString(State.StateFailedTransitionType));
	}

	DebugString += FString::Printf(TEXT("\nStates Items\n  [ %-30s | %-12s | %-30s | %15s | %10s ]\n"),
	TEXT("State"), TEXT("Type"), TEXT("Name"), TEXT("Bindings"), TEXT("Struct Idx"));
	for (const FBakedStateTreeState& State : StateTree->States)
	{
		// Tasks
		if (State.TasksNum)
		{
			for (int32 j = 0; j < State.TasksNum; j++)
			{
				const FStateTreeTaskBase& Task = GetItem(Storage, State.TasksBegin + j).Get<FStateTreeTaskBase>();
				DebugString += FString::Printf(TEXT("  | %-30s | %-12s | %-30s | %15s | %10d |\n"), *State.Name.ToString(),
					TEXT("  Task"), *Task.Name.ToString(), *Task.BindingsBatch.Describe(), Task.SourceStructIndex);
			}
		}

		// Evaluators
		if (State.EvaluatorsNum)
		{
			for (int32 EvalIndex = 0; EvalIndex < State.EvaluatorsNum; EvalIndex++)
			{
				const FStateTreeEvaluatorBase& Eval = GetItem(Storage, State.EvaluatorsBegin + EvalIndex).Get<FStateTreeEvaluatorBase>();
				DebugString += FString::Printf(TEXT("  | %-30s | %-12s | %-30s | %15s | %10d |\n"), *State.Name.ToString(),
					TEXT("  Evaluator"), *Eval.Name.ToString(), *Eval.BindingsBatch.Describe(), Eval.SourceStructIndex);
			}
		}
	}

	UE_LOG(LogStateTree, Log, TEXT("%s"), *DebugString);
}

FString FStateTreeExecutionContext::GetActiveStateName(FStateTreeItemView ExternalStorage) const
{
	if (!StateTree)
	{
		return FString(TEXT("<None>"));
	}
	const FStateTreeItemView Storage = SelectStorage(ExternalStorage);
	FStateTreeExecutionState& Exec = GetExecState(Storage);

	FString FullStateName;
	
	// Active States
	TStaticArray<FStateTreeHandle, 32> ActiveStates;
	const int32 NumStates = GetActiveStates(Exec.CurrentState, ActiveStates);

	for (int32 Index = 0; Index < NumStates; Index++)
	{
		FStateTreeHandle Handle = ActiveStates[Index];
		if (Handle.IsValid())
		{
			const FBakedStateTreeState& State = StateTree->States[Handle.Index];
			if (Index > 0)
			{
				FullStateName += TEXT("\n");
			}
			FullStateName += FString::Printf(TEXT("%*s-"), Index * 3, TEXT("")); // Indent
			FullStateName += *State.Name.ToString();
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
#endif // WITH_STATETREE_DEBUG

#undef STATETREE_LOG
#undef STATETREE_CLOG
