// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreeNodeBase.h"
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

	FStateTreeStateLink() = default;
	FStateTreeStateLink(EStateTreeTransitionType InType) : Type(InType) {}

	void Set(const EStateTreeTransitionType InType, const UStateTreeState* InState = nullptr);
	
	bool IsValid() const { return ID.IsValid(); }

	UPROPERTY(EditDefaultsOnly, Category = Link)
	FName Name;
	
	UPROPERTY(EditDefaultsOnly, Category = Link)
	FGuid ID;

	UPROPERTY(EditDefaultsOnly, Category = Link)
	EStateTreeTransitionType Type = EStateTreeTransitionType::GotoState;
};


/**
 * Base for Evaluator, Task and Condition nodes.
 */
USTRUCT()
struct STATETREEEDITORMODULE_API FStateTreeEditorNode
{
	GENERATED_BODY()

	void Reset()
	{
		Node.Reset();
		Instance.Reset();
		InstanceObject = nullptr;
		ID = FGuid();
	}

	FName GetName() const
	{
		if (const FStateTreeNodeBase* NodePtr = Node.GetPtr<FStateTreeNodeBase>())
		{
			return NodePtr->Name;
		}
		return FName();
	}

	UPROPERTY(EditDefaultsOnly, Category = Node)
	FInstancedStruct Node;

	UPROPERTY(EditDefaultsOnly, Category = Node)
	FInstancedStruct Instance;

	UPROPERTY(EditDefaultsOnly, Instanced, Category = Node)
	UObject* InstanceObject = nullptr;
	
	UPROPERTY(EditDefaultsOnly, Category = Node)
	FGuid ID;
};

template <typename T>
struct TStateTreeEditorNode : public FStateTreeEditorNode
{
	typedef T NodeType;
	FORCEINLINE T& GetItem() { return Node.template GetMutable<T>(); }
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
	TStateTreeEditorNode<T>& AddCondition(TArgs&&... InArgs)
	{
		FStateTreeEditorNode& CondNode = Conditions.AddDefaulted_GetRef();
		CondNode.ID = FGuid::NewGuid();
		CondNode.Node.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Cond = CondNode.Node.GetMutable<T>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Cond.GetInstanceDataType()))
		{
			CondNode.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeEditorNode<T>&>(CondNode);
	}

	UPROPERTY(EditDefaultsOnly, Category = Transition)
	EStateTreeTransitionEvent Event = EStateTreeTransitionEvent::OnCompleted;

	UPROPERTY(EditDefaultsOnly, Category = Transition)
	FStateTreeStateLink State;

	// Gate delay in seconds.
	UPROPERTY(EditDefaultsOnly, Category = Transition, meta = (UIMin = "0", ClampMin = "0", UIMax = "25", ClampMax = "25"))
	float GateDelay = 0.0f;
	
	UPROPERTY(EditDefaultsOnly, Category = Transition, meta = (BaseStruct = "StateTreeConditionBase", BaseClass = "StateTreeConditionBlueprintBase"))
	TArray<FStateTreeEditorNode> Conditions;
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
	TStateTreeEditorNode<T>& AddEnterCondition(TArgs&&... InArgs)
	{
		FStateTreeEditorNode& CondNode = EnterConditions.AddDefaulted_GetRef();
		CondNode.ID = FGuid::NewGuid();
		CondNode.Node.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Cond = CondNode.Node.GetMutable<T>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Cond.GetInstanceDataType()))
		{
			CondNode.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeEditorNode<T>&>(CondNode);
	}

	/**
	 * Adds Task of specified type.
	 * @return reference to the new Task. 
	 */
	template<typename T, typename... TArgs>
	TStateTreeEditorNode<T>& AddTask(TArgs&&... InArgs)
	{
		FStateTreeEditorNode& TaskItem = Tasks.AddDefaulted_GetRef();
		TaskItem.ID = FGuid::NewGuid();
		TaskItem.Node.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Task = TaskItem.Node.GetMutable<T>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Task.GetInstanceDataType()))
		{
			TaskItem.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeEditorNode<T>&>(TaskItem);
	}

	/**
	 * Adds Evaluator of specified type.
	 * @return reference to the new Evaluator. 
	 */
	template<typename T, typename... TArgs>
    TStateTreeEditorNode<T>& AddEvaluator(TArgs&&... InArgs)
	{
		FStateTreeEditorNode& EvalItem = Evaluators.AddDefaulted_GetRef();
		EvalItem.ID = FGuid::NewGuid();
		EvalItem.Node.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		T& Eval = EvalItem.Node.GetMutable<T>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(Eval.GetInstanceDataType()))
		{
			EvalItem.Instance.InitializeAs(InstanceType);
		}
		return static_cast<TStateTreeEditorNode<T>&>(EvalItem);
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

	UPROPERTY(EditDefaultsOnly, Category = "Enter Conditions", meta = (BaseStruct = "StateTreeConditionBase", BaseClass = "StateTreeConditionBlueprintBase"))
	TArray<FStateTreeEditorNode> EnterConditions;

	UPROPERTY(EditDefaultsOnly, Category = "Evaluators", meta = (BaseStruct = "StateTreeEvaluatorBase", BaseClass = "StateTreeEvaluatorBlueprintBase"))
	TArray<FStateTreeEditorNode> Evaluators;

	UPROPERTY(EditDefaultsOnly, Category = "Tasks", meta = (BaseStruct = "StateTreeTaskBase", BaseClass = "StateTreeTaskBlueprintBase"))
	TArray<FStateTreeEditorNode> Tasks;

	// Single item used when schema calls for single task per state.
	UPROPERTY(EditDefaultsOnly, Category = "Task", meta = (BaseStruct = "StateTreeTaskBase", BaseClass = "StateTreeTaskBlueprintBase"))
	FStateTreeEditorNode SingleTask;

	UPROPERTY(EditDefaultsOnly, Category = "Transitions")
	TArray<FStateTreeTransition> Transitions;

	UPROPERTY()
	TArray<UStateTreeState*> Children;

	UPROPERTY(meta = (ExcludeFromHash))
	bool bExpanded;

	UPROPERTY(meta = (ExcludeFromHash))
	UStateTreeState* Parent;

};
