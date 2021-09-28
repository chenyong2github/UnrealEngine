// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EngineDefines.h"
#include "StateTree.h"
#include "StateTreeInstance.generated.h"

class UStateTreeEvaluatorBase;
class UStateTreeTaskBase;
struct FStateTreeParameterStorage;
struct FStateTreeParameterLayout;
struct FStateTreeCondition;

/**
 * Runs StateTrees defined in UStateTree asset.
 * Uses constant data from StateTree, keeps local storage of variables, and creates instanced Evaluators and Tasks.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeInstance
{
	GENERATED_BODY()

public:
	FStateTreeInstance();
	~FStateTreeInstance();

	// Initializes the StateTree instance to be used with specific owner and StateTree asset.
	bool Init(UObject& Owner, const UStateTree& InStateTree);

	// Returns the StateTree asset in use.
	const UStateTree* GetStateTree() const { return StateTree; }

	// Returns owner of the StateTree instance.
	UObject* GetOwner() const { return Owner; }

	// Start executing.
	void Start();
	// Stop executing.
	void Stop();

	// Tick the state tree logic.
	EStateTreeRunStatus Tick(const float DeltaTime, FStateTreeParameterStorage* InputParameters = nullptr);

	// Returns value of a variable, or default value if handle is not valid.
	bool GetValueBool(const FStateTreeHandle Handle, const bool DefaultValue = false) const;
	int32 GetValueInt(const FStateTreeHandle Handle, const int32 DefaultValue = 0) const;
	float GetValueFloat(const FStateTreeHandle Handle, const float DefaultValue = 0.0f) const;
	const FVector& GetValueVector(const FStateTreeHandle Handle, const FVector& DefaultValue = FVector::ZeroVector) const;
	const FWeakObjectPtr& GetValueObject(const FStateTreeHandle Handle, const FWeakObjectPtr& DefaultValue = nullptr) const;
	bool GetValue(const FStateTreeHandle Handle, EStateTreeVariableType Type, uint8* Value) const;

	// Sets value of a variable, returns true if the value changed.
	bool SetValueBool(const FStateTreeHandle Handle, const bool Value);
	bool SetValueInt(const FStateTreeHandle Handle, const int32 Value);
	bool SetValueFloat(const FStateTreeHandle Handle, const float Value);
	bool SetValueVector(const FStateTreeHandle Handle, const FVector& Value);
	bool SetValueObject(const FStateTreeHandle Handle, FWeakObjectPtr Value);
	bool SetValue(const FStateTreeHandle Handle, EStateTreeVariableType Type, const uint8* Value);

	bool ReadParametersFromStorage(const FStateTreeParameterLayout& ParameterLayout, const FStateTreeParameterStorage& ParameterStorage);
	bool WriteParametersToStorage(const FStateTreeParameterLayout& ParameterLayout, FStateTreeParameterStorage& ParameterStorage) const;

#if ENABLE_VISUAL_LOG
	void DescribeSelfToVisLog(struct FVisualLogEntry& Snapshot) const;
#endif // ENABLE_VISUAL_LOG

#if WITH_GAMEPLAY_DEBUGGER
	FString GetDebugInfoString() const;
#endif // WITH_GAMEPLAY_DEBUGGER

protected:

	// Describes result from state selection logic, which can be a state to enter to, or Done/Failed status for the whole tree.
	struct FStateSelectionResult
	{
		FStateSelectionResult() : Status(EStateTreeRunStatus::Failed) {}
		FStateSelectionResult(FStateTreeHandle InState, EStateTreeRunStatus InStatus) : State(InState), Status(InStatus) {}
		FStateSelectionResult(FStateTreeHandle InState) : State(InState), Status(EStateTreeRunStatus::Running) {}
		FStateSelectionResult(EStateTreeRunStatus InStatus) : State(), Status(InStatus) {}

		bool IsValid() const { return Status == EStateTreeRunStatus::Running ? State.IsValid() : true; }

		FStateTreeHandle State;
		EStateTreeRunStatus Status;
	};

	// Resets the instance to initial empty state.
	void Reset();

	// Resets current active state, and calls ExitState() if needed.
	void ResetState();

	// Calls Activate on all Evaluators.
	void ActivateEvaluators();
	// Calls Deactivate on all Evaluators.
	void DeactivateEvaluators();
	// Ticks Evaluators by delta time.
	void TickEvaluators(const float DeltaTime);

	// Ticks specified state by delta time.
	EStateTreeRunStatus TickState(const FStateTreeHandle StateHandle, const float DeltaTime);

	// Handles logic for entering State.
	void EnterState(const FStateTreeHandle StateHandle);
	// Handles logic for exiting State.
	void ExitState(const FStateTreeHandle StateHandle);

	// Returns true if the specified condition passes.
	bool TestCondition(const FStateTreeCondition& Condition);

	// Returns true if all conditions pass.
	bool TestAllConditions(const uint32 ConditionsOffset, const uint32 ConditionsNum);

	// Walks towards the Root state and triggers the first transition which conditions pass and target state can be entered.
	// On valid transition SelectState() is executed starting at the transition target state, and resulting state is returned.
	// If no transition can be triggered, returns CurrentStateHandle.
	FStateSelectionResult TriggerTransitions(const FStateTreeHandle CurrentStateHandle);

	// Triggers default transitions for specified state. If the state has State Failed transition specified,
	// and StateTickStatus is Failed, the failure transition is tried to trigger. In other cases State Done
	// transition is tried to trigger (including the case that State Failed is unset). 
	// If target state cannot be entered or transition not valid, sets execution status to failed.
	// On valid transition SelectState() is executed starting at the transition target state, and resulting state is returned.
	FStateSelectionResult TriggerDefaultTransition(const FStateTreeHandle StateHandle, EStateTreeRunStatus StateTickStatus);

	// Runs state selection logic starting at the specified state, walking towards the leaf nodes.
	FStateSelectionResult SelectState(const FStateTreeHandle StateHandle);

	// Runs state selection logic on the children of the specified state, walking towards the leaf nodes.
	FStateSelectionResult SelectChildState(const FStateTreeHandle StateHandle);

	// Owner of the StateTree
	UPROPERTY(Transient)
	UObject* Owner;

	// The StateTree asset.
	UPROPERTY()
	const UStateTree* StateTree;

	// Evaluator instances
	UPROPERTY(Transient)
	TArray<UStateTreeEvaluatorBase*> Evaluators;

	// Task instances
	UPROPERTY(Transient)
	TArray<UStateTreeTaskBase*> Tasks;

	// Variable memory of the instance
	TArray<uint8> VariableMemoryPool;

	// Currently active state
	FStateTreeHandle CurrentState;

	EStateTreeRunStatus LastTickStatus;

	// Currently active task.
	FStateTreeHandle CurrentTask;

	// Task to active the next update.
	FStateTreeHandle NextTask;

	// Running status of the instance.
	EStateTreeRunStatus RunStatus;
};
