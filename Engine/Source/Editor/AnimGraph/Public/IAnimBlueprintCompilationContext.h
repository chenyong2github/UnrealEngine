// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetCompiler.h"

class UEdGraphNode;
class UEdGraph;
class UEdGraphPin;
struct FPoseLinkMappingRecord;
class UAnimGraphNode_Base;
class FCompilerResultsLog;
class UBlueprint;
class UAnimBlueprint;
class FProperty;
struct FKismetCompilerOptions;
class IAnimBlueprintCompilerHandler;

#define ANIM_FUNC_DECORATOR	TEXT("__AnimFunc")

/** Interface to the anim BP compiler context for use while compilation is in progress */
class ANIMGRAPH_API IAnimBlueprintCompilationContext
{
public:
	virtual ~IAnimBlueprintCompilationContext() {}

	// Get a compilation context from a kismet compiler context assuming that it is an FAnimBlueprintCompilerContext
	static TUniquePtr<IAnimBlueprintCompilationContext> Get(FKismetCompilerContext& InKismetCompiler);

	// Get a handler of the specified type and name (i.e. via simple name-based RTTI)
	// Handlers are registered via IAnimBlueprintCompilerHandlerCollection::RegisterHandler
	template <typename THandlerClass>
	THandlerClass* GetHandler(FName InName) const
	{
		return static_cast<THandlerClass*>(GetHandlerInternal(InName));
	}

	// Spawns an intermediate node associated with the source node (for error purposes)
	template <typename NodeType>
	NodeType* SpawnIntermediateNode(UEdGraphNode* SourceNode, UEdGraph* ParentGraph = nullptr)
	{
		return GetKismetCompiler()->SpawnIntermediateNode<NodeType>(SourceNode, ParentGraph);
	}

	// Spawns an intermediate event node associated with the source node (for error purposes)
	template <typename NodeType>
	NodeType* SpawnIntermediateEventNode(UEdGraphNode* SourceNode, UEdGraphPin* SourcePin = nullptr, UEdGraph* ParentGraph = nullptr)
	{
		return GetKismetCompiler()->SpawnIntermediateEventNode<NodeType>(SourceNode, SourcePin, ParentGraph);
	}

	// Find a property in the currently-compiled class
	template <typename FieldType>
	FieldType* FindClassFProperty(const TCHAR* InFieldPath) const
	{
		return FindFProperty<FieldType>(GetKismetCompiler()->NewClass, InFieldPath);
	}

	// Adds a pose link mapping record
	void AddPoseLinkMappingRecord(const FPoseLinkMappingRecord& InRecord) { AddPoseLinkMappingRecordImpl(InRecord); }

	// Process the passed-in list of nodes
	void ProcessAnimationNodes(TArray<UAnimGraphNode_Base*>& AnimNodeList) { ProcessAnimationNodesImpl(AnimNodeList); }

	// Prunes any nodes that aren't reachable via a pose link
	void PruneIsolatedAnimationNodes(const TArray<UAnimGraphNode_Base*>& RootSet, TArray<UAnimGraphNode_Base*>& GraphNodes) { PruneIsolatedAnimationNodesImpl(RootSet, GraphNodes); }

	// Perform an expansion step for the specified graph
	void ExpansionStep(UEdGraph* Graph, bool bAllowUbergraphExpansions) { ExpansionStepImpl(Graph, bAllowUbergraphExpansions); }

	// Get the message log for the current compilation
	FCompilerResultsLog& GetMessageLog() const { return GetMessageLogImpl(); }

	// Performs standard validation on the graph (outputs point to inputs, no more than one connection to each input, types match on both ends, etc...)
	bool ValidateGraphIsWellFormed(UEdGraph* Graph) const { return ValidateGraphIsWellFormedImpl(Graph); }

	// Returns the allocation index of the specified node, processing it if it was pending
	int32 GetAllocationIndexOfNode(UAnimGraphNode_Base* VisualAnimNode) const { return GetAllocationIndexOfNodeImpl(VisualAnimNode); }

	// Get the currently-compiled blueprint
	const UBlueprint* GetBlueprint() const { return GetBlueprintImpl(); }

	// Get the currently-compiled anim blueprint
	const UAnimBlueprint* GetAnimBlueprint() const { return GetAnimBlueprintImpl(); }

	// Get the consolidated uber graph during compilation
	UEdGraph* GetConsolidatedEventGraph() const { return GetConsolidatedEventGraphImpl(); }

	// Gets all anim graph nodes that are piped into the provided node (traverses input pins)
	void GetLinkedAnimNodes(UAnimGraphNode_Base* InGraphNode, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes) const { return GetLinkedAnimNodesImpl(InGraphNode, LinkedAnimNodes); }

	// Index of the nodes (must match up with the runtime discovery process of nodes, which runs thru the property chain)
	const TMap<UAnimGraphNode_Base*, int32>& GetAllocatedAnimNodeIndices() const { return GetAllocatedAnimNodeIndicesImpl(); }

	// Map of true source objects (user edited ones) to the cloned ones that are actually compiled
	const TMap<UAnimGraphNode_Base*, UAnimGraphNode_Base*>& GetSourceNodeToProcessedNodeMap() const { return GetSourceNodeToProcessedNodeMapImpl(); }

	// Map of anim node indices to node properties
	const TMap<int32, FProperty*>& GetAllocatedPropertiesByIndex() const { return GetAllocatedPropertiesByIndexImpl(); }

	// Map of anim node indices to node properties
	const TMap<UAnimGraphNode_Base*, FProperty*>& GetAllocatedPropertiesByNode() const { return GetAllocatedPropertiesByNodeImpl(); }

protected:
	// Adds a pose link mapping record
	virtual void AddPoseLinkMappingRecordImpl(const FPoseLinkMappingRecord& InRecord) = 0;

	// Process the passed-in list of nodes
	virtual void ProcessAnimationNodesImpl(TArray<UAnimGraphNode_Base*>& AnimNodeList) = 0;

	// Prunes any nodes that aren't reachable via a pose link
	virtual void PruneIsolatedAnimationNodesImpl(const TArray<UAnimGraphNode_Base*>& RootSet, TArray<UAnimGraphNode_Base*>& GraphNodes) = 0;

	// Perform an expansion step for the specified graph
	virtual void ExpansionStepImpl(UEdGraph* Graph, bool bAllowUbergraphExpansions) = 0;

	// Get the message log for the current compilation
	virtual FCompilerResultsLog& GetMessageLogImpl() const = 0;

	// Performs standard validation on the graph (outputs point to inputs, no more than one connection to each input, types match on both ends, etc...)
	virtual bool ValidateGraphIsWellFormedImpl(UEdGraph* Graph) const = 0;

	// Returns the allocation index of the specified node, processing it if it was pending
	virtual int32 GetAllocationIndexOfNodeImpl(UAnimGraphNode_Base* VisualAnimNode) const = 0;

	// Get the currently-compiled blueprint
	virtual const UBlueprint* GetBlueprintImpl() const = 0;

	// Get the currently-compiled anim blueprint
	virtual const UAnimBlueprint* GetAnimBlueprintImpl() const = 0;

	// Get the consolidated uber graph during compilation
	virtual UEdGraph* GetConsolidatedEventGraphImpl() const = 0;

	// Gets all anim graph nodes that are piped into the provided node (traverses input pins)
	virtual void GetLinkedAnimNodesImpl(UAnimGraphNode_Base* InGraphNode, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes) const = 0;

	// Index of the nodes (must match up with the runtime discovery process of nodes, which runs thru the property chain)
	virtual const TMap<UAnimGraphNode_Base*, int32>& GetAllocatedAnimNodeIndicesImpl() const = 0;

	// Map of true source objects (user edited ones) to the cloned ones that are actually compiled
	virtual const TMap<UAnimGraphNode_Base*, UAnimGraphNode_Base*>& GetSourceNodeToProcessedNodeMapImpl() const = 0;

	// Map of anim node indices to node properties
	virtual const TMap<int32, FProperty*>& GetAllocatedPropertiesByIndexImpl() const = 0;

	// Map of anim node indices to node properties
	virtual const TMap<UAnimGraphNode_Base*, FProperty*>& GetAllocatedPropertiesByNodeImpl() const = 0;

	// GetHandler helper function
	virtual IAnimBlueprintCompilerHandler* GetHandlerInternal(FName InName) const = 0;

	// Get the compiler as a base class to avoid circular include issues with templated functions/classes
	virtual FKismetCompilerContext* GetKismetCompiler() const = 0;
};
