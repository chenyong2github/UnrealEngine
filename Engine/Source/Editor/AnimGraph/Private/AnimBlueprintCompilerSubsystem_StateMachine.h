// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimBlueprintCompilerSubsystem.h"

#include "AnimBlueprintCompilerSubsystem_StateMachine.generated.h"

class UK2Node_CallFunction;
class UK2Node_AnimGetter;
class UK2Node_TransitionRuleGetter;
class UAnimGraphNode_Base;
class UAnimStateTransitionNode;
class UEdGraph;
class UEdGraphNode;
struct FAnimNotifyEvent;

UCLASS()
class UAnimBlueprintCompilerSubsystem_StateMachine : public UAnimBlueprintCompilerSubsystem
{
	GENERATED_BODY()

public:
	// Finds or adds a notify event triggered from a state machine
	int32 FindOrAddNotify(FAnimNotifyEvent& Notify);

	// This function does the following steps:
	//   Clones the nodes in the specified source graph
	//   Merges them into the ConsolidatedEventGraph
	//   Processes any animation nodes
	//   Returns the index of the processed cloned version of SourceRootNode
	//	 If supplied, will also return an array of all cloned nodes
	int32 ExpandGraphAndProcessNodes(UEdGraph* SourceGraph, UAnimGraphNode_Base* SourceRootNode, UAnimStateTransitionNode* TransitionNode = nullptr, TArray<UEdGraphNode*>* ClonedNodes = nullptr);

private:
	// UAnimBlueprintCompilerSubsystem interface
	virtual void PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes) override;
	virtual void PostProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes) override;
	virtual bool ShouldProcessFunctionGraph(UEdGraph* InGraph) const override;

	// Spawns a function call node, calling a function on the anim instance
	UK2Node_CallFunction* SpawnCallAnimInstanceFunction(UEdGraphNode* SourceNode, FName FunctionName);

	// Convert transition getters into a function call/etc...
	void ProcessTransitionGetter(UK2Node_TransitionRuleGetter* Getter, UAnimStateTransitionNode* TransitionNode);

	// Automatically fill in parameters for the specified Getter node
	void AutoWireAnimGetter(UK2Node_AnimGetter* Getter, UAnimStateTransitionNode* InTransitionNode);
	
private:
	// List of getter node's we've found so the auto-wire can be deferred till after state machine compilation
	TArray<UK2Node_AnimGetter*> FoundGetterNodes;

	// Preprocessed lists of getters from the root of the ubergraph
	TArray<UK2Node_TransitionRuleGetter*> RootTransitionGetters;
	TArray<UK2Node_AnimGetter*> RootGraphAnimGetters;
};