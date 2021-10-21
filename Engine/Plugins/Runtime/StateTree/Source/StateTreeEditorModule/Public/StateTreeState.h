// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "Misc/Guid.h"
#include "InstancedStruct.h"
#include "StateTreeState.generated.h"

class UStateTreeState;

/**
 * Editor representation of a link to another state in StateTree
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeStateLink
{
	GENERATED_BODY()
public:
	FStateTreeStateLink() : Type(EStateTreeTransitionType::GotoState) {}
	FStateTreeStateLink(EStateTreeTransitionType InType) : Type(InType) {}

	void Set(const EStateTreeTransitionType InType, const UStateTreeState* InState = nullptr);
	
	bool IsValid() const { return ID.IsValid(); }

	UPROPERTY(EditDefaultsOnly, Category = Link)
	FName Name;
	
	UPROPERTY(EditDefaultsOnly, Category = Link)
	FGuid ID;

	UPROPERTY(EditDefaultsOnly, Category = Transition)
	EStateTreeTransitionType Type;
};

/**
 * Helper struct for Evaluator details customization (i.e. show summary when collapsed)
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeEvaluatorItem
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = Evaluator, meta = (BaseStruct = "StateTreeEvaluator2Base", ExcludeBaseStruct))
	FInstancedStruct Type;
};

/**
 * Helper struct for Task details customization (i.e. show summary when collapsed)
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeTaskItem
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = Task, meta = (BaseStruct = "StateTreeTask2Base", ExcludeBaseStruct))
	FInstancedStruct Type;
};


/**
 * Helper struct for Condition details customization (i.e. show summary when collapsed)
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeConditionItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Condition, meta = (BaseStruct = "StateTreeConditionBase", ExcludeBaseStruct))
	FInstancedStruct Type;
};

/**
 * Editor representation of a transition in StateTree
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeTransition
{
	GENERATED_BODY()

	FStateTreeTransition() = default;
	FStateTreeTransition(const EStateTreeTransitionEvent InEvent, const EStateTreeTransitionType InType, const UStateTreeState* InState = nullptr);

	template<typename T, typename... TArgs>
	T& AddCondition(TArgs&&... InArgs)
	{
		FStateTreeConditionItem& CondItem = Conditions.AddDefaulted_GetRef();
		CondItem.Type.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Condition = CondItem.Type.GetMutable<T>();
		Condition.ID = FGuid::NewGuid();
		return Condition;
	}

	UPROPERTY(EditDefaultsOnly, Category = Transition)
	EStateTreeTransitionEvent Event = EStateTreeTransitionEvent::OnCondition;

	UPROPERTY(EditDefaultsOnly, Category = Transition)
	FStateTreeStateLink State;

	// Gate delay in seconds.
	UPROPERTY(EditDefaultsOnly, Category = Transition, meta = (UIMin = "0", ClampMin = "0", UIMax = "25", ClampMax = "25"))
	float GateDelay = 0.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = Transition)
	TArray<FStateTreeConditionItem> Conditions;
};


/**
 * Editor representation of a state in StateTree
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories)
class STATETREEEDITORMODULE_API UStateTreeState : public UObject
{
	GENERATED_BODY()

public:
	UStateTreeState(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	const FStateTreeTaskBase* GetTaskByID(FGuid ID) const;
	FStateTreeTaskBase* GetTaskByID(FGuid ID);

	UStateTreeState* GetNextSiblingState() const;

	// StateTree Builder API
	
	/** Adds child state with specified name. */
	UStateTreeState& AddChildState(const FName ChildName)
	{
		UStateTreeState* ChildState = NewObject<UStateTreeState>(this);
		check(ChildState);
		ChildState->Name = ChildName;
		ChildState->Parent = this;
		Children.Add(ChildState);
		return *ChildState;
	}

	/**
	 * Adds enter condition of specified type.
	 * @return reference to the new condition. 
	 */
	template<typename T, typename... TArgs>
	T& AddEnterCondition(TArgs&&... InArgs)
	{
		FStateTreeConditionItem& CondItem = EnterConditions.AddDefaulted_GetRef();
		CondItem.Type.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		return CondItem.Type.GetMutable<T>();
	}

	/**
	 * Adds Task of specified type.
	 * @return reference to the new Task. 
	 */
	template<typename T, typename... TArgs>
	T& AddTask(TArgs&&... InArgs)
	{
		FStateTreeTaskItem& TaskItem = Tasks.AddDefaulted_GetRef();
		TaskItem.Type.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Task = TaskItem.Type.GetMutable<T>();
		Task.ID = FGuid::NewGuid();
		return Task;
	}

	/**
	 * Adds Evaluator of specified type.
	 * @return reference to the new Evaluator. 
	 */
	template<typename T, typename... TArgs>
    T& AddEvaluator(TArgs&&... InArgs)
	{
		FStateTreeEvaluatorItem& EvalItem = Evaluators.AddDefaulted_GetRef();
		EvalItem.Type.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Eval = EvalItem.Type.GetMutable<T>();
		Eval.ID = FGuid::NewGuid();
		return Eval;
	}

	/**
	 * Adds Transition.
	 * @return reference to the new Transition. 
	 */
	FStateTreeTransition& AddTransition(const EStateTreeTransitionEvent InEvent, const EStateTreeTransitionType InType, const UStateTreeState* InState = nullptr)
	{
		return Transitions.Emplace_GetRef(InEvent, InType, InState);
	}

	// ~StateTree Builder API

	UPROPERTY(EditDefaultsOnly, Category = State)
	FName Name;

	UPROPERTY()
	FGuid ID;

	UPROPERTY(EditDefaultsOnly, Category = "Enter Conditions")
	TArray<FStateTreeConditionItem> EnterConditions;

	UPROPERTY(EditDefaultsOnly, Category = "Evaluators")
	TArray<FStateTreeEvaluatorItem> Evaluators;

	UPROPERTY(EditDefaultsOnly, Category = "Tasks")
	TArray<FStateTreeTaskItem> Tasks;

	UPROPERTY(EditDefaultsOnly, Category = "Transitions")
	TArray<FStateTreeTransition> Transitions;

	UPROPERTY()
	TArray<UStateTreeState*> Children;

	UPROPERTY()
	bool bExpanded;

	UPROPERTY()
	UStateTreeState* Parent;

};
