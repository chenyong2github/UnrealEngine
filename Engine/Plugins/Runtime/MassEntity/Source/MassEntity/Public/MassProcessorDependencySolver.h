// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"


class UMassProcessor;

enum class EDependencyNodeType : uint8
{
	Invalid,
	Processor,
	GroupStart,
	GroupEnd
};

struct MASSENTITY_API FProcessorDependencySolver
{
private:
	struct FNode
	{
		FNode(const FName InName, UMassProcessor* InProcessor) 
			: Name(InName), Processor(InProcessor)
		{}

		FName Name = TEXT("");
		TArray<FNode> SubNodes;
		TMap<FName, int32> Indices;
		UMassProcessor* Processor = nullptr;
		TArray<int32> OriginalDependencies;
		TArray<int32> TransientDependencies;
		TArray<FName> ExecuteBefore;
		TArray<FName> ExecuteAfter;

		int32 FindOrAddGroupNodeIndex(const FString& GroupName);
		int32 FindNodeIndex(FName InNodeName) const;
		bool HasDependencies() const;
	};

public:
	struct FOrderInfo
	{
		FName Name = TEXT("");
		UMassProcessor* Processor = nullptr;
		EDependencyNodeType NodeType = EDependencyNodeType::Invalid;
		TArray<FName> Dependencies;
	};

	FProcessorDependencySolver(TArrayView<UMassProcessor*> InProcessors, const FName Name, const FString& InDependencyGraphFileName = FString());
	void ResolveDependencies(TArray<FOrderInfo>& OutResult, TConstArrayView<const FName> PriorityNodes = TConstArrayView<const FName>());

	static void CreateSubGroupNames(FName InGroupName, TArray<FString>& SubGroupNames);

protected:
	// note that internals are protected rather than private to support unit testing

	/**
	 * Traverses RootNode's child nodes indicated by InOutIndicesRemaining and appends to OutNodeIndices the ones that 
	 * have no dependencies. The indices added to OutNodeIndices also get removed from remaining nodes' outstanding 
	 * dependencies.
	 * Note that the whole InOutIndicesRemaining gets tested in sequence, which means nodes can get their dependencies 
	 * emptied and added to OutNodeIndices within one call (as opposed to PerformPrioritySolverStep).
	 */
	static int32 PerformSolverStep(FNode& RootNode, TArray<int32>& InOutIndicesRemaining, TArray<int32>& OutNodeIndices);
	
	/**
	 * Traverses InOutIndicesRemaining in search of the first RootNode's node that has no dependencies left. Once found 
	 * the node's index gets added to OutNodeIndices, removed from dependency lists from all other nodes and the function 
	 * quits.
	 * @return 'true' if a dependency-less node has been found and added to OutNodeIndices; 'false' otherwise.
	 */
	static bool PerformPrioritySolverStep(FNode& RootNode, TArray<int32>& InOutIndicesRemaining, TArray<int32>& OutNodeIndices);
	
	static FString NameViewToString(TConstArrayView<FName> View);

	void AddNode(FName GroupName, UMassProcessor& Processor);
	void BuildDependencies(FNode& RootNode);
	void Solve(FNode& RootNode, TConstArrayView<const FName> PriorityNodes, TArray<FProcessorDependencySolver::FOrderInfo>& OutResult, int LoggingIndent = 0);
	void LogNode(const FNode& RootNode, const FNode* ParentNode = nullptr, int Indent = 0);
	void DumpGraph(FArchive& LogFile) const;
	
	TArrayView<UMassProcessor*> Processors;
	FNode GroupRootNode;
	bool bAnyCyclesDetected = false;
	FString DependencyGraphFileName;

	friend struct FDumpGraphDependencyUtils;
};