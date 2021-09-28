// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "StateTreeTypes.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeEvaluatorBase.generated.h"

struct FStateTreeInstance;
struct FStateTreeExecutionContext;
struct FStateTreeVariableLayout;
struct FStateTreeConstantStorage;

/**
 * Base class of StateTree Evaluators.
 * Evaluators calculate and expose data to be used for decision making in a StateTree.
 */
UCLASS(Abstract)
class STATETREEMODULE_API UStateTreeEvaluatorBase : public UObject
{
	GENERATED_BODY()

public:
	UStateTreeEvaluatorBase(const FObjectInitializer& ObjectInitializer);

	// Called when instantiated first time.
	virtual bool Initialize(FStateTreeInstance& StateTreeInstance) PURE_VIRTUAL(UStateTreeEvaluatorBase::Initialize, return false; );
	// Called when evaluator becomes active/ticking.
	virtual void Activate(FStateTreeInstance & StateTreeInstance) PURE_VIRTUAL(UStateTreeEvaluatorBase::Activate, return; );
	// Called when evaluator becomes inactive.
	virtual void Deactivate(FStateTreeInstance & StateTreeInstance) PURE_VIRTUAL(UStateTreeEvaluatorBase::Deactivate, return; );

	// Called on each tick.
	virtual void Tick(FStateTreeInstance & StateTreeInstance, const float DeltaTime) PURE_VIRTUAL(UStateTreeEvaluatorBase::Tick, return; );

	// Returns ID of the Evaluator
	const FName& GetName() const { return Name; }

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString & DebugString, const FStateTreeInstance & StateTreeInstance) const;
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_EDITOR
	// Defines output variables in variable map.
	virtual void DefineOutputVariables(FStateTreeVariableLayout& Variables) const {};
	// Create Evaluator instance template, set default variables, and resolve variable handles.
	virtual bool ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants, UObject* Outer) PURE_VIRTUAL(UStateTreeEvaluatorBase::Resolve, return false; );
	// Returns ture if changed.
	virtual bool ValidateParameterLayout() { return false; }

	const FGuid GetID() const { return ID; }

	void SetNewUniqueID() { ID = FGuid::NewGuid(); }
	void SetName(const FName& InName) { Name = InName; }

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	UPROPERTY(EditDefaultsOnly, Category = Evaluator)
	FName Name;

#if WITH_EDITORONLY_DATA
	UPROPERTY(meta = (IgnoreForMemberInitializationTest))
	FGuid ID;
#endif
};


// STATETREE_V2

/**
 * Base struct of StateTree Evaluators.
 * Evaluators calculate and expose data to be used for decision making in a StateTree.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeEvaluator2Base // TODO: change to FStateTreeEvaluatorBase once UStateTreeEvaluatorBase is removed
{
	GENERATED_BODY()

	FStateTreeEvaluator2Base() = default;

	virtual ~FStateTreeEvaluator2Base() {}

	/**
	 * Called when a new state is entered and evaluator is part of active states. The change type parameter describes if the evaluator's state
	 * was previously part of the list of active states (Sustained), or if it just became active (Changed).
	 * @param Context Reference to current execution context.
	 * @param ChangeType Describes the change type (Changed/Sustained).
	 * @param Transition Describes the states involved in the transition
	 */
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) {}

	/**
	 * Called when a current state is exited and evaluator is part of active states. The change type parameter describes if the evaluator's state
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
	 * Called when evaluator needs to be updated. EvalType describes if the tick happens during state tree tick when the evaluator is on active state (Tick),
	 * or during state selection process when the evaluator's state is visited while it's inactive (PreSelection).
	 * That is, type "Tick" means that the call happens between EnterState()/ExitState() pair, "PreSelection" is used otherwise.
	 * @param Context Reference to current execution context.
	 * @param EvalType Describes tick type.
	 * @param DeltaTime Time since last StateTree tick, or 0 if called during preselection.
	 */
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) {}

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const;
#endif // WITH_GAMEPLAY_DEBUGGER

	UPROPERTY(EditDefaultsOnly, Category = Evaluator, meta=(EditCondition = "false", EditConditionHides))
	FName Name;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, Category = Evaluator, meta=(IgnoreForMemberInitializationTest, EditCondition="false", EditConditionHides))	// Hack, we want the ID to be found as IPropertyHandle, but do not want to display it.
	FGuid ID;
#endif

	UPROPERTY()
	FStateTreeHandle BindingsBatch = FStateTreeHandle::Invalid;		// Property binding copy batch handle.

	UPROPERTY()
	uint16 SourceStructIndex = 0;									// Property binding Source Struct index of the evaluator.
};
template<> struct TStructOpsTypeTraits<FStateTreeEvaluator2Base> : public TStructOpsTypeTraitsBase2<FStateTreeEvaluator2Base> { enum { WithPureVirtual = true, }; };

// ~STATETREE_V2
