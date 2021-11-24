// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "StateTreeTypes.h"
#include "StateTreeTaskBase.generated.h"

struct FStateTreeExecutionContext;

/**
 * Base struct for StateTree Tasks.
 * Tasks are logic executed in an active state.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeTaskBase
{
	GENERATED_BODY()

	FStateTreeTaskBase() = default;

	virtual ~FStateTreeTaskBase() {}

	/**
	* @return Struct that represents the runtime data of the evaluator.
	*/
	virtual const UStruct* GetInstanceDataType() const PURE_VIRTUAL(FStateTreeTaskBase::GetInstanceDataType(), return nullptr;);

	/**
	 * Called when the StateTree asset is linked. Allows to resolve references to other StateTree data.
	 * @see TStateTreeExternalDataHandle
	 * @see TStateTreeInstanceDataPropertyHandle
	 * @param Linker Reference to the linker
	 * @return true if linking succeeded. 
	 */
	virtual bool Link(FStateTreeLinker& Linker) { return true; }

	/**
	 * Called when a new state is entered and task is part of active states. The change type parameter describes if the task's state
	 * was previously part of the list of active states (Sustained), or if it just became active (Changed).
	 * @param Context Reference to current execution context.
	 * @param ChangeType Describes the change type (Changed/Sustained).
	 * @param Transition Describes the states involved in the transition
	 * @return Succeed/Failed will end the state immediately and trigger to select new state, Running will carry on to tick the state.
	 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const { return EStateTreeRunStatus::Running; }

	/**
	* Called when a current state is exited and task is part of active states. The change type parameter describes if the task's state
	* will be active after the transition (Sustained), or if it will became inactive (Changed).
	* @param Context Reference to current execution context.
	* @param ChangeType Describes the change type (Changed/Sustained).
	* @param Transition Describes the states involved in the transition
	*/
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const {}

	/**
	 * Called Right after a state has been completed. StateCompleted is called in reverse order to allow to propagate state to Evaluators and Tasks that
	 * are executed earlier in the tree. Note that StateCompleted is not called if conditional transition changes the state.
	 * @param Context Reference to current execution context.
	 * @param CompletionStatus Describes the running status of the completed state (Succeeded/Failed).
	 * @param CompletedState Handle of the state that was completed.
	 */
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const {}

	/**
	* Called during state tree tick when the task is on active state.
	* @param Context Reference to current execution context.
	* @param DeltaTime Time since last StateTree tick.
	* @return Running status of the state: Running if still in progress, Succeeded if execution is done and succeeded, Failed if execution is done and failed.
	*/
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const { return EStateTreeRunStatus::Running; };

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const;
#endif

	UPROPERTY(EditAnywhere, Category = Task, meta = (EditCondition = "false", EditConditionHides))
	FName Name;

	/** Property binding copy batch handle. */
	UPROPERTY()
	FStateTreeHandle BindingsBatch = FStateTreeHandle::Invalid;

	/** The runtime data's data view index in the StateTreeExecutionContext, and source struct index in property binding. */
	UPROPERTY()
	uint16 DataViewIndex = 0;

	UPROPERTY()
	uint16 InstanceIndex = 0;

	UPROPERTY()
	uint8 bInstanceIsObject : 1;
};

template<> struct TStructOpsTypeTraits<FStateTreeTaskBase> : public TStructOpsTypeTraitsBase2<FStateTreeTaskBase> { enum { WithPureVirtual = true, }; };
