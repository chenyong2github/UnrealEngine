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

	bool CreateStates();
	bool CreateStateRecursive(UStateTreeState& State, const FStateTreeHandle Parent);
	bool CreateStateTransitions();
	bool CreateCondition(const FStateTreeEditorNode& CondNode);
	bool CreateTask(const FStateTreeEditorNode& TaskNode);
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
