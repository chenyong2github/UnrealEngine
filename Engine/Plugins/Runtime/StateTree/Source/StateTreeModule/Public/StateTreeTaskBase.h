// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "StateTreeTypes.h"
#include "StateTreeTaskBase.generated.h"

struct FStateTreeInstance;
struct FStateTreeExecutionContext;
struct FStateTreeVariableLayout;
struct FStateTreeConstantStorage;

/**
 * Base class for StateTree Tasks.
 * Tasks are logic executed at an active state.
 */
UCLASS(Abstract)
class STATETREEMODULE_API UStateTreeTaskBase : public UObject
{
	GENERATED_BODY()

public:
	UStateTreeTaskBase(const FObjectInitializer& ObjectInitializer);

	// Called when instantiated first time.
	virtual bool Initialize(FStateTreeInstance& StateTreeInstance) PURE_VIRTUAL(UStateTreeTaskBase::Initialize, return false; );
	// Called when evaluator becomes active/ticking.
	virtual void Activate(FStateTreeInstance& StateTreeInstance) PURE_VIRTUAL(UStateTreeTaskBase::Activate, return; );
	// Called when evaluator becomes inactive.
	virtual void Deactivate(FStateTreeInstance& StateTreeInstance) PURE_VIRTUAL(UStateTreeTaskBase::Deactivate, return; );

	// Called on each tick.
	virtual EStateTreeRunStatus Tick(FStateTreeInstance& StateTreeInstance, const float DeltaTime) PURE_VIRTUAL(UStateTreeTaskBase::Tick, return EStateTreeRunStatus::Failed; );

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const;
#endif

#if WITH_EDITOR
	// Create Task instance template, set default variables, and resolve variable handles.
	virtual bool ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants, UObject* Outer) PURE_VIRTUAL(UStateTreeTaskBase::Resolve, return false; );
	// Returns ture if changed.
	virtual bool ValidateParameterLayout() { return false; }

	virtual void PostLoad() override;
#endif

	void SetNewUniqueID() { ID = FGuid::NewGuid(); }

	UPROPERTY(EditAnywhere, Category = Task)
	FName Name;

	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	FGuid ID;
};


// STATETREE_V2

/**
 * Base stuct for StateTree Tasks.
 * Tasks are logic executed in an active state.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeTask2Base // TODO: change to FStateTreeTaskBase once UStateTreeTaskBase is removed
{
	GENERATED_BODY()

	FStateTreeTask2Base() = default;

	virtual ~FStateTreeTask2Base() {}

	/**
	 * Called when a new state is entered and task is part of active states. The change type parameter describes if the task's state
	 * was previously part of the list of active states (Sustained), or if it just became active (Changed).
	 * @param Context Reference to current execution context.
	 * @param ChangeType Describes the change type (Changed/Sustained).
	 * @param Transition Describes the states involved in the transition
	 * @return Succeed/Failed will end the state immediately and trigger to select new state, Running will carry on to tick the state.
	 */
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) { return EStateTreeRunStatus::Running; }

	/**
	* Called when a current state is exited and task is part of active states. The change type parameter describes if the task's state
	* will be active after the transition (Sustained), or if it will became inactive (Changed).
	* @param Context Reference to current execution context.
	* @param ChangeType Describes the change type (Changed/Sustained).
	* @param Transition Describes the states involved in the transition
	*/
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) {}

	/**
	 * Called Right after a state has been completed. StateCompleted is called in reverse order to allow to propagate state to Evaluators and Tasks that
	 * are executed earlier in the tree. Note that StateCompleted is not called if conditional transition changes the state.
	 * @param Context Reference to current execution context.
	 * @param CompletionStatus Describes the running status of the completed state (Succeeded/Failed).
	 * @param CompletedState Handle of the state that was completed.
	 */
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) {}

	/**
	* Called during state tree tick when the task is on active state.
	* @param Context Reference to current execution context.
	* @param DeltaTime Time since last StateTree tick.
	* @return Running status of the state: Running if still in progress, Succeeded if execution is done and succeeded, Failed if execution is done and failed.
	*/
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) { return EStateTreeRunStatus::Failed; };

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const;
#endif

	UPROPERTY(EditAnywhere, Category = Task, meta = (EditCondition = "false", EditConditionHides))
	FName Name;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Task, meta = (IgnoreForMemberInitializationTest, EditCondition = "false", EditConditionHides))	// Hack, we want the ID to be found as IPropertyHandle, but do not want to display it.
	FGuid ID;
#endif

	UPROPERTY()
	FStateTreeHandle BindingsBatch = FStateTreeHandle::Invalid;	// Property binding copy batch handle.

	UPROPERTY()
	uint16 SourceStructIndex = 0;								// Property binding Source Struct index of the task.
};
template<> struct TStructOpsTypeTraits<FStateTreeTask2Base> : public TStructOpsTypeTraitsBase2<FStateTreeTask2Base> { enum { WithPureVirtual = true, }; };

// ~STATETREE_V2
