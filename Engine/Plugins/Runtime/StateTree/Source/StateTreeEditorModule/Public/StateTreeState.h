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

	UPROPERTY(EditDefaultsOnly, Category = Link)
	EStateTreeTransitionType Type;
};

/**
 * Helper struct for Evaluator details customization (i.e. show summary when collapsed)
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeEvaluatorItem
{
	GENERATED_BODY()

	FName GetName() const
	{
		if (const FStateTreeEvaluatorBase* Eval = Item.GetPtr<FStateTreeEvaluatorBase>())
		{
			return Eval->Name;
		}
		return FName();
	}

	UPROPERTY(EditDefaultsOnly, Category = Evaluator, meta = (BaseStruct = "StateTreeEvaluatorBase", ExcludeBaseStruct))
	FInstancedStruct Item;

	UPROPERTY(EditDefaultsOnly, Category = Evaluator)
	FInstancedStruct Instance;

	UPROPERTY(EditDefaultsOnly, Category = Evaluator)
	FGuid ID;
};

template <typename T>
struct STATETREEEDITORMODULE_API TStateTreeEvaluatorItem : public FStateTreeEvaluatorItem
{
	typedef T ItemType;
	FORCEINLINE T& GetItem() { return Item.template GetMutable<T>(); }
	FORCEINLINE typename T::InstanceDataType& GetInstance() { return Instance.template GetMutable<typename T::InstanceDataType>(); }
};


/**
 * Helper struct for Task details customization (i.e. show summary when collapsed)
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeTaskItem
{
	GENERATED_BODY()

	FName GetName() const
	{
		if (const FStateTreeTaskBase* Task = Item.GetPtr<FStateTreeTaskBase>())
		{
			return Task->Name;
		}
		return FName();
	}
	
	UPROPERTY(EditDefaultsOnly, Category = Task, meta = (BaseStruct = "StateTreeTaskBase", ExcludeBaseStruct))
	FInstancedStruct Item;

	UPROPERTY(EditDefaultsOnly, Category = Task)
	FInstancedStruct Instance;

	UPROPERTY(EditDefaultsOnly, Category = Task)
	FGuid ID;
};

template <typename T>
struct STATETREEEDITORMODULE_API TStateTreeTaskItem : public FStateTreeTaskItem
{
	typedef T ItemType;
	FORCEINLINE T& GetItem() { return Item.template GetMutable<T>(); }
	FORCEINLINE typename T::InstanceDataType& GetInstance() { return Instance.template GetMutable<typename T::InstanceDataType>(); }
};

/**
 * Helper struct for Condition details customization (i.e. show summary when collapsed)
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeConditionItem
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = Condition, meta = (BaseStruct = "StateTreeConditionBase", ExcludeBaseStruct))
	FInstancedStruct Item;

	UPROPERTY(EditDefaultsOnly, Category = Condition)
	FInstancedStruct Instance;

	UPROPERTY(EditDefaultsOnly, Category = Condition)
	FGuid ID;
};

template <typename T>
struct STATETREEEDITORMODULE_API TStateTreeConditionItem : public FStateTreeConditionItem
{
	typedef T ItemType;
	FORCEINLINE T& GetItem() { return Item.template GetMutable<T>(); }
	FORCEINLINE typename T::InstanceDataType& GetInstance() { return Instance.template GetMutable<typename T::InstanceDataType>(); }
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
	TStateTreeConditionItem<T>& AddCondition(TArgs&&... InArgs)
	{
		FStateTreeConditionItem& CondItem = Conditions.AddDefaulted_GetRef();
		CondItem.ID = FGuid::NewGuid();
		CondItem.Item.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Cond = CondItem.Item.GetMutable<T>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Cond.GetInstanceDataType()))
		{
			CondItem.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeConditionItem<T>&>(CondItem);
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
	TStateTreeConditionItem<T>& AddEnterCondition(TArgs&&... InArgs)
	{
		FStateTreeConditionItem& CondItem = EnterConditions.AddDefaulted_GetRef();
		CondItem.ID = FGuid::NewGuid();
		CondItem.Item.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Cond = CondItem.Item.GetMutable<T>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Cond.GetInstanceDataType()))
		{
			CondItem.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeConditionItem<T>&>(CondItem);
	}

	/**
	 * Adds Task of specified type.
	 * @return reference to the new Task. 
	 */
	template<typename T, typename... TArgs>
	TStateTreeTaskItem<T>& AddTask(TArgs&&... InArgs)
	{
		FStateTreeTaskItem& TaskItem = Tasks.AddDefaulted_GetRef();
		TaskItem.ID = FGuid::NewGuid();
		TaskItem.Item.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Task = TaskItem.Item.GetMutable<T>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Task.GetInstanceDataType()))
		{
			TaskItem.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeTaskItem<T>&>(TaskItem);
	}

	/**
	 * Adds Evaluator of specified type.
	 * @return reference to the new Evaluator. 
	 */
	template<typename T, typename... TArgs>
    TStateTreeEvaluatorItem<T>& AddEvaluator(TArgs&&... InArgs)
	{
		FStateTreeEvaluatorItem& EvalItem = Evaluators.AddDefaulted_GetRef();
		EvalItem.ID = FGuid::NewGuid();
		EvalItem.Item.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Eval = EvalItem.Item.GetMutable<T>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Eval.GetInstanceDataType()))
		{
			EvalItem.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeEvaluatorItem<T>&>(EvalItem);
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

	UPROPERTY(EditDefaultsOnly, Category = "State")
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
