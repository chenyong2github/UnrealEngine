// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "StateTreePropertyBindingCompiler.h"
#include "StateTreeCompilerLog.h"

class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;
struct FStateTreeEditorNode;
struct FStateTreeStateLink;

/**
 * Helper class to convert StateTree editor representation into a compact baked data.
 * Holds data needed during baking.
 */
struct STATETREEEDITORMODULE_API FStateTreeBaker
{
public:

	FStateTreeBaker(FStateTreeCompilerLog& InLog)
		: Log(InLog)
	{
	}
	
	bool Bake(UStateTree& InStateTree);

private:

	bool ResolveTransitionState(const UStateTreeState& SourceState, const FStateTreeStateLink& Link, FStateTreeHandle& OutTransitionHandle) const;
	FStateTreeHandle GetStateHandle(const FGuid& StateID) const;
	UStateTreeState* GetState(const FGuid& StateID);
	static FString GetExecutionPathString(const TConstArrayView<const UStateTreeState*> Path);
	static bool IsPathLinked(const TConstArrayView<const UStateTreeState*> Path);

	bool CreateExecutionInfos();
	bool CreateExecutionInfosRecursive(UStateTreeState& State, TArray<const UStateTreeState*>& Path);
	bool CreateStates();
	bool CreateStateRecursive(UStateTreeState& State, const FStateTreeHandle Parent);
	
	bool CreateStateTasks();
	bool CreateStateEvaluators();
	bool CreateStateTransitions();
	
	bool CreateConditions(UStateTreeState& State, TConstArrayView<FStateTreeEditorNode> Conditions);
	bool CreateCondition(UStateTreeState& State, const FStateTreeEditorNode& CondNode, const EStateTreeConditionOperand Operand, const int8 DeltaIndent);
	bool CreateTask(UStateTreeState& State, const FStateTreeEditorNode& TaskNode);
	bool CreateEvaluator(UStateTreeState& State, const FStateTreeEditorNode& EvalNode);
	bool GetAndValidateBindings(UStateTreeState& State, const FStateTreeBindableStructDesc& TargetStruct, TArray<FStateTreeEditorPropertyBinding>& OutBindings) const;
	bool IsPropertyAnyEnum(const FStateTreeBindableStructDesc& Struct, FStateTreeEditorPropertyPath Path) const;

	struct FExecutionPath
	{
		FExecutionPath() = default;
		FExecutionPath(const TConstArrayView<const UStateTreeState*> InPath) : Path(InPath) {}
		
		TArray<const UStateTreeState*> Path;
	};

	struct FStateExecutionInfo
	{
		TArray<FExecutionPath> ExecutionPaths;
	};

	FStateTreeCompilerLog& Log;
	UStateTree* StateTree = nullptr;
	UStateTreeEditorData* TreeData = nullptr;
	TMap<FGuid, int32> IDToState;
	TArray<UStateTreeState*> SourceStates;
	TMap<UStateTreeState*, FStateExecutionInfo> ExecutionInfos;
	FStateTreePropertyBindingCompiler BindingsCompiler;
};
