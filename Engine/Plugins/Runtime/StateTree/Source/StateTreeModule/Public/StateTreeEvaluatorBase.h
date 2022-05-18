// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreeNodeBase.h"
#include "StateTreeEvaluatorBase.generated.h"

struct FStateTreeExecutionContext;

/**
 * Base struct of StateTree Evaluators.
 * Evaluators calculate and expose data to be used for decision making in a StateTree.
 */
USTRUCT(meta = (Hidden))
struct STATETREEMODULE_API FStateTreeEvaluatorBase : public FStateTreeNodeBase
{
	GENERATED_BODY()

	/**
	 * Called when a new state is entered and evaluator is part of active states. The change type parameter describes if the evaluator's state
	 * was previously part of the list of active states (Sustained), or if it just became active (Changed).
	 * @param Context Reference to current execution context.
	 * @param ChangeType Describes the change type (Changed/Sustained).
	 * @param Transition Describes the states involved in the transition
	 */
	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const {}

	/**
	 * Called when a current state is exited and evaluator is part of active states. The change type parameter describes if the evaluator's state
	 * will be active after the transition (Sustained), or if it will became inactive (Changed).
	 * @param Context Reference to current execution context.
	 * @param ChangeType Describes the change type (Changed/Sustained).
	 * @param Transition Describes the states involved in the transition
	 */
	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const {}

	/**
	 * Called Right after a state has been completed. StateCompleted is called in reverse order to allow to propagate state to Evaluators and Tasks that
	 * are executed earlier in the tree. Note that StateCompleted is not called if conditional transition changes the state.
	 * @param Context Reference to current execution context.
	 * @param CompletionStatus Describes the running status of the completed state (Succeeded/Failed).
	 * @param CompletedActiveStates Active states at the time of completion.
	 */
	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeActiveStates& CompletedActiveStates) const {}
	
	UE_DEPRECATED(5.1, "This function will be removed for 5.1, use Tick() instead.")
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const { }

	
	/**
	 * Called when StateTree is started.
	 * @param Context Reference to current execution context.
	 */
	virtual void TreeStart(FStateTreeExecutionContext& Context) const {}

	/**
	 * Called when StateTree is stopped.
	 * @param Context Reference to current execution context.
	 */
	virtual void TreeStop(FStateTreeExecutionContext& Context) const {}

	/**
	 * Called each frame to update the evaluator.
	 * @param Context Reference to current execution context.
	 * @param DeltaTime Time since last StateTree tick, or 0 if called during preselection.
	 */
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Evaluate(Context, EStateTreeEvaluationType::Tick, DeltaTime);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const;
#endif // WITH_GAMEPLAY_DEBUGGER
};

/**
* Base class (namespace) for all common Evaluators that are generally applicable.
* This allows schemas to safely include all Evaluators child of this struct. 
*/
USTRUCT(Meta=(Hidden))
struct STATETREEMODULE_API FStateTreeEvaluatorCommonBase : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()
};