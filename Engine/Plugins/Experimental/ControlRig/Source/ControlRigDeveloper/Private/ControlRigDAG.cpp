// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigDAG.h"
#include "ControlRig.h"

DEFINE_LOG_CATEGORY_STATIC(LogControlRigDAG, Log, All);

FControlRigDAG::FControlRigDAG()
	: bSortIsRequired(false)
{
}

void FControlRigDAG::AddNode(bool InIsMutable, bool InIsOutputParameter, const FName& InName)
{
	Nodes.Add(FNode(InName, Nodes.Num(), InIsMutable, InIsOutputParameter));
}

void FControlRigDAG::AddLink(const int32 FromNode, const int32 ToNode, const int32 FromOrder, const int32 ToOrder)
{
	check(FromNode < Nodes.Num());
	check(ToNode < Nodes.Num());

	FPin FromPin(FromNode, ToOrder, Links.Num());
	FPin ToPin(ToNode, FromOrder, Links.Num());
	Links.Add(TPair<FPin, FPin>(FromPin, ToPin));
	Nodes[ToPin.Node].Inputs.Add(FromPin);
	Nodes[FromPin.Node].Outputs.Add(ToPin);

	bSortIsRequired = true;
}

bool FControlRigDAG::TopologicalSort(TArray<FNode>& OutOrder, TArray<FNode>& OutPotentialCycle)
{
	SortIfRequired();

	OutPotentialCycle = FindCycle();
	if (OutPotentialCycle.Num() > 0)
	{
		return false;
	}

	struct Local
	{
		static void VisitNode(const FNode& Node, TArray<bool>& Visited, TArray<FNode>& SortedNodes, const TArray<FNode>& Nodes)
		{
			if (Visited[Node.Index])
			{
				return;
			}

			Visited[Node.Index] = true;

			for (const FPin& Pin : Node.Inputs)
			{
				VisitNode(Nodes[Pin.Node], Visited, SortedNodes, Nodes);
			}

			SortedNodes.Push(Node);

			if (Node.IsMutable)
			{
				for (const FPin& Pin : Node.Outputs)
				{
					VisitNode(Nodes[Pin.Node], Visited, SortedNodes, Nodes);
				}
			}
		}
	};

	// find all of the left mutable nodes on the left
	TArray<FNode> LeafNodes;
	TArray<FNode> OutputParameterNodes;
	for (const FNode& Node : Nodes)
	{
		if (Node.IsOutputParameter)
		{
			OutputParameterNodes.Add(Node);
		}
		else if (Node.IsMutable)
		{
			if (Node.Inputs.Num() == 0)
			{
				LeafNodes.Add(Node);
			}
		}
	}

	LeafNodes.Append(OutputParameterNodes);

	check(LeafNodes.Num() > 0);

	TArray<bool> NodeVisited;
	NodeVisited.SetNumZeroed(Nodes.Num());
	OutOrder.Reset();
	for (const FNode& LeafNode : LeafNodes)
	{
		Local::VisitNode(LeafNode, NodeVisited, OutOrder, Nodes);
	}
	return true;
}

int32 FControlRigDAG::GetMaxDistanceToLeafOutput(int32 NodeIndex) const
{
	if(Nodes[NodeIndex].Outputs.Num() == 0)
	{
		return 0;
	}

	int32 MaxDistance = 0;
	for (const FPin& Pin : Nodes[NodeIndex].Outputs)
	{
		int32 Distance = GetMaxDistanceToLeafOutput(Pin.Node);
		MaxDistance = FMath::Max<int32>(Distance, MaxDistance);
	}
	return MaxDistance + 1;
}

// Finds the cycles as node index arrays
TArray<FControlRigDAG::FNode> FControlRigDAG::FindCycle()
{
	CycleWhiteList.Reset();
	CycleGreyList.Reset();
	CycleBlackList.Reset();
	CycleWhiteList.Reserve(Nodes.Num());
	CycleGreyList.Reserve(Nodes.Num());
	CycleBlackList.Reserve(Nodes.Num());
	CycleDepthTraversal.Reset();
	Cycle.Reset();

	// mark all nodes to be on the white list (the non-visited list)
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); NodeIndex++)
	{
		CycleWhiteList.Add(NodeIndex);
	}

	while (CycleBlackList.Num() < Nodes.Num())
	{
		// start a new pass by finding the first node in the white list
		int32 Node = -1;
		for (const int32& WhiteListNode : CycleWhiteList)
		{
			Node = WhiteListNode;
			CycleDepthTraversal.Add(Node, -1);
			CycleWhiteList.Remove(Node);
			CycleGreyList.Add(Node);
			break;
		}

		if(IsNodeCyclic(Node))
		{
			break;
		}
	}

	return Cycle;
}

// this performs a depth first traversal by walking the output
// links of each node
bool FControlRigDAG::IsNodeCyclic(int32 NodeIndex)
{
	for (const FPin& Pin : Nodes[NodeIndex].Outputs)
	{
		int32 NeighborIndex = Pin.Node;
		if (CycleBlackList.Contains(NeighborIndex))
		{
			continue;
		}
		else if (CycleWhiteList.Contains(NeighborIndex))
		{
			CycleDepthTraversal.Add(NeighborIndex, NodeIndex);
			CycleWhiteList.Remove(NeighborIndex);
			CycleGreyList.Add(NeighborIndex);
			if(IsNodeCyclic(NeighborIndex))
			{
				return true;
			}
		}
		else if (CycleGreyList.Contains(NeighborIndex))
		{
			// this means we've detected a cycle
			while(NodeIndex != -1)
			{
				Cycle.Add(Nodes[NodeIndex]);
				NodeIndex = CycleDepthTraversal.FindChecked(NodeIndex);
				if (NodeIndex == NeighborIndex)
				{
					Cycle.Add(Nodes[NodeIndex]);
					break;
				}
			}
			return true;
		}
	}

	// if the current node has no neighbors we clear the grey list
	CycleGreyList.Remove(NodeIndex);
	CycleBlackList.Add(NodeIndex);
	return false;
}

void FControlRigDAG::SortIfRequired()
{
	if (!bSortIsRequired)
	{
		return;
	}

	auto ExtractOrder = [](FPin Pin) -> int32
	{
		return Pin.Order;
	};

	for (int32 NodeIndex=0; NodeIndex < Nodes.Num();NodeIndex++)
	{
		Algo::SortBy(Nodes[NodeIndex].Inputs, ExtractOrder);
	}

	bSortIsRequired = false;
}

void FControlRigDAG::DumpDag()
{
	UE_LOG(LogControlRigDAG, Display, TEXT("FControlRigDAG DAG;"));
	for (const FNode& Node : Nodes)
	{
		UE_LOG(LogControlRigDAG, Display, TEXT("DAG.AddNode(%s);"), Node.IsMutable ? TEXT("true") : TEXT("false"));
	}

	for (const TPair<FPin, FPin>& Link : Links)
	{
		UE_LOG(LogControlRigDAG, Display, TEXT("DAG.AddLink(%d, %d, %d, %d);"), Link.Key.Node, Link.Value.Node, Link.Value.Order, Link.Key.Order);
	}
}
