// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimBlueprintCompilerHandler.h"

class UK2Node_CallFunction;
class UK2Node_AnimGetter;
class UK2Node_TransitionRuleGetter;
class UAnimGraphNode_Base;
class UAnimStateTransitionNode;
class UEdGraph;
class UEdGraphNode;
struct FAnimNotifyEvent;
class IAnimBlueprintCompilerCreationContext;
class IAnimBlueprintGeneratedClassCompiledData;
class IAnimBlueprintCompilationContext;

class FAnimBlueprintCompilerHandler_StateMachine : public IAnimBlueprintCompilerHandler
{
public:
	FAnimBlueprintCompilerHandler_StateMachine(IAnimBlueprintCompilerCreationContext& InCreationContext);

	// This function does the following steps:
	//   Clones the nodes in the specified source graph
	//   Merges them into the ConsolidatedEventGraph
	//   Processes any animation nodes
	//   Returns the index of the processed cloned version of SourceRootNode
	//	 If supplied, will also return an array of all cloned nodes
	int32 ExpandGraphAndProcessNodes(UEdGraph* SourceGraph, UAnimGraphNode_Base* SourceRootNode, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData, UAnimStateTransitionNode* TransitionNode = nullptr, TArray<UEdGraphNode*>* ClonedNodes = nullptr);

private:
	void PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
	void PostProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);

	// Spawns a function call node, calling a function on the anim instance
	UK2Node_CallFunction* SpawnCallAnimInstanceFunction(IAnimBlueprintCompilationContext& InCompilationContext, UEdGraphNode* SourceNode, FName FunctionName);

	// Convert transition getters into a function call/etc...
	void ProcessTransitionGetter(UK2Node_TransitionRuleGetter* Getter, UAnimStateTransitionNode* TransitionNode, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);

	// Automatically fill in parameters for the specified Getter node
	void AutoWireAnimGetter(UK2Node_AnimGetter* Getter, UAnimStateTransitionNode* InTransitionNode, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
	
private:
	// List of getter node's we've found so the auto-wire can be deferred till after state machine compilation
	TArray<UK2Node_AnimGetter*> FoundGetterNodes;

	// Preprocessed lists of getters from the root of the ubergraph
	TArray<UK2Node_TransitionRuleGetter*> RootTransitionGetters;
	TArray<UK2Node_AnimGetter*> RootGraphAnimGetters;
};