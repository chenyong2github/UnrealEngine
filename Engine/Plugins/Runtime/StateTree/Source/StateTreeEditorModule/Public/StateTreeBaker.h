// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "StateTreePropertyBindingCompiler.h"

class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;
struct FStateTreeCondition;
class UStateTreeEvaluatorBase;
class UStateTreeTaskBase;
struct FStateTreeConditionItem;
struct FStateTreeStateLink;

/**
 * Helper class to convert StateTree editor representation into a compact baked data.
 * Holds data needed during baking.
 */
struct STATETREEEDITORMODULE_API FStateTreeBaker
{
public:
	bool Bake(UStateTree& InStateTree);
	bool Bake2(UStateTree& InStateTree);

private:
	enum class EBindingContainerType : uint8
	{
		Evaluator,
		Task,
	};

	// Helper struct used when collecting evaluators, keeping the state reference so that we can have better errors.
	struct FSourceEvaluator
	{
		FSourceEvaluator(UStateTreeEvaluatorBase* InEval, UStateTreeState* InState) : Eval(InEval), State(InState) {}
		UStateTreeEvaluatorBase* Eval = nullptr;
		UStateTreeState* State = nullptr;
	};

	// Helper struct used when collecting tracks, keeping the state reference so that we can have better errors.
	struct FSourceTask
	{
		FSourceTask(UStateTreeTaskBase* InTask, UStateTreeState* InState) : Task(InTask), State(InState) {}
		UStateTreeTaskBase* Task = nullptr;
		UStateTreeState* State = nullptr;
	};

	void DefineParameters() const;
	void ResolveParameters() const;
	bool CreateStates();
	bool CreateStateRecursive(UStateTreeState& State, const FStateTreeHandle Parent);
	bool ResolveTransitionState(const UStateTreeState& SourceState, const TCHAR* ContextStr, const FStateTreeStateLink& Link, FStateTreeHandle& OutTransitionHandle) const;
	bool CreateStateTransitions();
	bool CreateCondition(const FStateTreeCondition& Cond);
	FStateTreeHandle GetStateHandle(const FGuid& StateID) const;

	bool CreateStates2();
	bool CreateStateRecursive2(UStateTreeState& State, const FStateTreeHandle Parent);
	bool CreateStateTransitions2();
	bool CreateCondition2(const FStateTreeConditionItem& CondItem);
	bool GetAndValidateBindings(const FStateTreeBindableStructDesc& TargetStruct, TArray<FStateTreeEditorPropertyBinding>& OutBindings) const;
	bool IsPropertyAnyEnum(const FStateTreeBindableStructDesc& Struct, FStateTreeEditorPropertyPath Path) const;
	bool CreateExternalItemHandles(FStructView Item);

	UStateTree* StateTree = nullptr;
	UStateTreeEditorData* TreeData = nullptr;
	TMap<FGuid, int32> IDToState;
	TArray<UStateTreeState*> SourceStates;
	TArray<FSourceEvaluator> SourceEvaluators;
	TArray<FSourceTask> SourceTasks;
	FStateTreePropertyBindingCompiler BindingsCompiler;
};
