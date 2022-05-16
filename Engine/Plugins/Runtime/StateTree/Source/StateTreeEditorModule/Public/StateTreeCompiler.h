// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreePropertyBindingCompiler.h"
#include "StateTreeCompilerLog.h"

class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;
struct FStateTreeEditorNode;
struct FStateTreeStateLink;

/**
 * Helper class to convert StateTree editor representation into a compact data.
 * Holds data needed during compiling.
 */
struct STATETREEEDITORMODULE_API FStateTreeCompiler
{
public:
	explicit FStateTreeCompiler(FStateTreeCompilerLog& InLog)
		: Log(InLog)
	{
	}
	
	bool Compile(UStateTree& InStateTree);
private:

	bool ResolveTransitionState(const UStateTreeState& SourceState, const FStateTreeStateLink& Link, FStateTreeHandle& OutTransitionHandle) const;
	FStateTreeHandle GetStateHandle(const FGuid& StateID) const;
	UStateTreeState* GetState(const FGuid& StateID);

	bool CreateStates();
	bool CreateStateRecursive(UStateTreeState& State, const FStateTreeHandle Parent);
	
	bool CreateEvaluators();
	bool CreateStateTasksAndParameters();
	bool CreateStateTransitions();
	
	bool CreateConditions(UStateTreeState& State, TConstArrayView<FStateTreeEditorNode> Conditions);
	bool CreateCondition(UStateTreeState& State, const FStateTreeEditorNode& CondNode, const EStateTreeConditionOperand Operand, const int8 DeltaIndent);
	bool CreateTask(UStateTreeState& State, const FStateTreeEditorNode& TaskNode);
	bool CreateEvaluator(const FStateTreeEditorNode& EvalNode);
	bool GetAndValidateBindings(const FStateTreeBindableStructDesc& TargetStruct, TArray<FStateTreeEditorPropertyBinding>& OutBindings) const;
	bool IsPropertyAnyEnum(const FStateTreeBindableStructDesc& Struct, FStateTreeEditorPropertyPath Path) const;

	FStateTreeCompilerLog& Log;
	UStateTree* StateTree = nullptr;
	UStateTreeEditorData* TreeData = nullptr;
	TMap<FGuid, int32> IDToState;
	TArray<UStateTreeState*> SourceStates;
	FStateTreePropertyBindingCompiler BindingsCompiler;
};
