// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/Subsystem.h"
#include "KismetCompiler.h"
#include "Containers/ArrayView.h"

#include "AnimBlueprintCompilerSubsystem.generated.h"

struct FPoseLinkMappingRecord;
class FAnimBlueprintCompilerContext;
class UAnimBlueprint;
class UAnimGraphNode_Base;
class UAnimBlueprintGeneratedClass;
class UAnimBlueprintClassSubsystem;

UCLASS()
class ANIMGRAPH_API UAnimBlueprintCompilerSubsystem : public USubsystem
{
	GENERATED_BODY()

public:

	/** Begin ordered calls - these functions are called int he order presented here */

	/** Start compiling the class */
	virtual void StartCompilingClass(UClass* InClass) {}

	/** Give the subsystem a chance to perform processing before all animation nodes are processed */
	virtual void PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes) {}

	/** Give the subsystem a chance to perform processing once all animation nodes have been processed */
	virtual void PostProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes) {}
	
	/** Give the subsystem a chance to perform processing post-expansion step */
	virtual void PostExpansionStep(UEdGraph* InGraph) {}

	/** Finish compiling the class */
	virtual void FinishCompilingClass(UClass* InClass) {}

	/** Copy any data into the CDO */
	virtual void CopyTermDefaultsToDefaultObject(UObject* InDefaultObject) {}

	/** End ordered calls */

	/** Gives a subsystem the option to skip the processing of a function graph (in general because it is expected to process the function graph itself somehow) */
	virtual bool ShouldProcessFunctionGraph(UEdGraph* InGraph) const { return true; }

	// Get all the class subsystems that we want to add to the class to support this subsystem
	// Note that this is called regardless of anim graph node connectivity so the subsystem will 
	// always be added even for isolated nodes
	virtual void GetRequiredClassSubsystems(TArray<TSubclassOf<UAnimBlueprintClassSubsystem>>& OutSubsystemClasses) const {}

	// Get the currently-compiled blueprint
	UBlueprint* GetBlueprint() const;

	// Get the currently-compiled anim blueprint
	UAnimBlueprint* GetAnimBlueprint() const;

	// Get the currently-compiled anim blueprint class
	UAnimBlueprintGeneratedClass* GetNewAnimBlueprintClass() const;

	// Get the message log for the current compilation
	FCompilerResultsLog& GetMessageLog() const;

	// Get the consolidated uber graph during compilation
	UEdGraph* GetConsolidatedEventGraph() const;

	// Performs standard validation on the graph (outputs point to inputs, no more than one connection to each input, types match on both ends, etc...)
	bool ValidateGraphIsWellFormed(UEdGraph* Graph) const;

	// Returns the allocation index of the specified node, processing it if it was pending
	int32 GetAllocationIndexOfNode(UAnimGraphNode_Base* VisualAnimNode);

	// Adds a pose link mapping record
	void AddPoseLinkMappingRecord(const FPoseLinkMappingRecord& InRecord);

	// Gets all anim graph nodes that are piped into the provided node (traverses input pins)
	void GetLinkedAnimNodes(UAnimGraphNode_Base* InGraphNode, TArray<UAnimGraphNode_Base*> &LinkedAnimNodes);

	// Index of the nodes (must match up with the runtime discovery process of nodes, which runs thru the property chain)
	const TMap<UAnimGraphNode_Base*, int32>& GetAllocatedAnimNodeIndices() const;

	// Map of true source objects (user edited ones) to the cloned ones that are actually compiled
	const TMap<UAnimGraphNode_Base*, UAnimGraphNode_Base*>& GetSourceNodeToProcessedNodeMap() const;

	// Map of anim node indices to node properties
	const TMap<int32, FProperty*>& GetAllocatedPropertiesByIndex() const;

	// Map of anim node indices to node properties
	const TMap<UAnimGraphNode_Base*, FProperty*>& GetAllocatedPropertiesByNode() const;

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
	
	// Expands split pins for a graph
	void ExpandSplitPins(UEdGraph* InGraph);

	// Process the passed-in list of nodes
	void ProcessAnimationNodes(TArray<UAnimGraphNode_Base*>& AnimNodeList);

	// Prunes any nodes that aren't reachable via a pose link
	void PruneIsolatedAnimationNodes(const TArray<UAnimGraphNode_Base*>& RootSet, TArray<UAnimGraphNode_Base*>& GraphNodes);

	// Perform an expansion step for the specified graph
	void ExpansionStep(UEdGraph* Graph, bool bAllowUbergraphExpansions);

	// Get another subsystem of the specified type
	template <typename TSubsystemClass>
	TSubsystemClass* GetSubsystem() const
	{
		return Cast<TSubsystemClass>(GetSubsystemInternal(GetKismetCompiler(), TSubsystemClass::StaticClass()));
	}

	// Get another subsystem of the specified type, assuming that the supplied context is an anim BP context
	template <typename TSubsystemClass>
	static TSubsystemClass* GetSubsystem(const FKismetCompilerContext& InCompilerContext)
	{
		return Cast<TSubsystemClass>(GetSubsystemInternal(&InCompilerContext, TSubsystemClass::StaticClass()));
	}

	// Find the first subsystem implementing the specified interface
	template<typename InterfaceClass>
	InterfaceClass* FindSubsystemWithInterface() const
	{
		return Cast<InterfaceClass>(FindSubsystemWithInterfaceInternal(GetKismetCompiler(), InterfaceClass::UClassType::StaticClass()));
	}

	// Find the first subsystem implementing the specified interface, assuming that the supplied context is an anim BP context
	template<typename InterfaceClass>
	static InterfaceClass* FindSubsystemWithInterface(const FKismetCompilerContext& InCompilerContext)
	{
		return Cast<InterfaceClass>(FindSubsystemWithInterfaceInternal(&InCompilerContext, InterfaceClass::UClassType::StaticClass()));
	}

	// Get the compiler options we are currently using
	const FKismetCompilerOptions& GetCompileOptions() const;

private:
	// Subsystem helper functions
	static UAnimBlueprintCompilerSubsystem* GetSubsystemInternal(const FKismetCompilerContext* CompilerContext, TSubclassOf<UAnimBlueprintCompilerSubsystem> InClass);
	static UAnimBlueprintCompilerSubsystem* FindSubsystemWithInterfaceInternal(const FKismetCompilerContext* CompilerContext, TSubclassOf<UInterface> InInterfaceClass);

private:
	/** USubsystem interface */
	virtual void Initialize(FSubsystemCollectionBase& InCollection) override;

	// Get the compiler as a base class to avoid circular include issues with templated functions/classes
	FKismetCompilerContext* GetKismetCompiler() const;

private:
	/** The compiler context that hosts this subsystem */
	FAnimBlueprintCompilerContext* CompilerContext;
};
