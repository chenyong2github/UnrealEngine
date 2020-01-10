// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The Control Rig Property traverser is used to determine the order
 * of execution for a graph described purely by the property links.
 * The traverser uses information about the mutability of the nodes
 * as well as the order of links on each node to figure out the order.
 * The rules here are as follows:
 * - Sort the input leaf mutable nodes (BeginExecution) by their 
 *   maximum graph distance (distance to leaf output node)
 * - Walk them from left to right
 * - For each mutable node traverse it's inputs based on the pin order
 *   and finally execute it.
 * - For each non-mutable node traverse it's inputs based on the pin order
 */
class CONTROLRIGDEVELOPER_API FControlRigDAG
{
public:

	// A single pin within the traverser. The Pin has access to 
	// it's node, the order on where is on the node and the index
	// of the link it belongs to.
	struct CONTROLRIGDEVELOPER_API FPin
	{
		int32 Node;
		int32 Order;
		int32 Link;

		FPin(const int32 InNode, const int32 InOrder, const int32 InLinkIndex)
			: Node(InNode)
			, Order(InOrder)
			, Link(InLinkIndex)
		{
		}
	};

	typedef TArray<FPin> FPinArray;

	// A node within the traverser identified by index
	// The IsMutable flag determines if the node is mutable or a
	// BeginExecution node.
	struct CONTROLRIGDEVELOPER_API FNode
	{
		FName Name;
		int32 Index;
		bool IsMutable;
		bool IsOutputParameter;
		FPinArray Inputs;
		FPinArray Outputs;

		FNode(const FName& InName, const int32 InIndex, const bool InIsMutable, const bool InIsOutputParameter)
			: Name(InName)
			, Index(InIndex)
			, IsMutable(InIsMutable)
			, IsOutputParameter(InIsOutputParameter)
		{
		}

		bool operator ==(const FNode& Other) const {
			return Name == Other.Name;
		}
	};

	TArray<FNode> Nodes;
	TArray<TPair<FPin, FPin>> Links;

	FControlRigDAG();

	// add a node to this traverser
	void AddNode(bool InIsMutable = false, bool InIsOutputParameter = false, const FName& InName = NAME_None);

	// add a link between two nodes given the node indices and the pin orders
	void AddLink(const int32 FromNode, const int32 ToNode, const int32 FromOrder, const int32 ToOrder);

	// returns the distance for a given node to the output node farthest away
	int32 GetMaxDistanceToLeafOutput(int32 NodeIndex) const;

	// returns true if the graph can be sorted and stores the order of execution in the order array
	// return false if there's a cycle in the graph and stores the cycle in the potentialcycle array
	bool TopologicalSort(TArray<FNode>& OutOrder, TArray<FNode>& OutPotentialCycle);

	// Finds the first cycle as a node index. Returns an empty array if there's no cycle
	TArray<FNode> FindCycle();

private:

	void SortIfRequired();

	void DumpDag();

	bool IsNodeCyclic(int32 NodeIndex);

	TSet<int32> CycleWhiteList;
	TSet<int32> CycleGreyList;
	TSet<int32> CycleBlackList;
	TMap<int32, int32> CycleDepthTraversal;
	TArray<FNode> Cycle;
	bool bSortIsRequired;

	friend class FControlRigBlueprintCompilerContext;
};
