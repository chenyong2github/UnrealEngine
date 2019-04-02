// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigDAG.h"
#include "ControlRig.h"

DEFINE_LOG_CATEGORY_STATIC(LogControlRigDAG, Log, All);

FControlRigDAG::FControlRigDAG()
{
}

void FControlRigDAG::AddNode(bool InIsMutable)
{
	Nodes.Add(FNode(Nodes.Num(), InIsMutable));
	NodeInputs.Add(FPinMap());
	NodeOutputs.Add(FPinMap());
}

void FControlRigDAG::AddLink(const int32 FromNode, const int32 ToNode, const int32 FromOrder, const int32 ToOrder)
{
	check(FromNode < Nodes.Num());
	check(ToNode < Nodes.Num());

	FPin FromPin(FromNode, FromOrder, Links.Num());
	FPin ToPin(ToNode, ToOrder, Links.Num());
	Links.Add(TPair<FPin, FPin>(FromPin, ToPin));
	NodeInputs[ToPin.Node].Add(ToPin.Order, FromPin);
	NodeOutputs[FromPin.Node].Add(FromPin.Order, ToPin);
}

bool FControlRigDAG::TopologicalSort(TArray<int32>& OutOrder, TArray<int32>& OutPotentialCycle)
{
	OutPotentialCycle = FindCycle();
	if (OutPotentialCycle.Num() > 0)
	{
		return false;
	}

	// find all of the mutable nodes without any mutable inputs
	TArray<int32> MutableLeafNodes;
	for (int32 Node = 0; Node < NodeOutputs.Num(); ++Node)
	{
		if (!Nodes[Node].IsMutable)
		{
			continue;
		}
		int32 NumMutableInputs = 0;
		for (FPinMap::TConstIterator Iter = NodeInputs[Node].CreateConstIterator(); Iter; ++Iter)
		{
			const FPin& InputPin = Iter.Value();
			if (Nodes[InputPin.Node].IsMutable)
			{
				NumMutableInputs++;
			}
		}
		if (NumMutableInputs > 0)
		{
			continue;
		}
		MutableLeafNodes.Add(Node);
	}
	check(MutableLeafNodes.Num() > 0);

	TMultiMap<int32, int32> SortedMutableNodes;
	for (int32 MutableNode : MutableLeafNodes)
	{
		int32 InvDistance = Nodes.Num() - GetMaxDistanceToLeafOutput(MutableNode);
		SortedMutableNodes.Add(InvDistance, MutableNode);
	}

	struct Local
	{
		static void VisitNode(int32 Node, TArray<bool>& Visited, TArray<int32>& SortedNodes, const TArray<FPinMap>& NodeInputs, const TArray<FPinMap>& NodeOutputs)
		{
			if (Visited[Node])
			{
				return;
			}

			Visited[Node] = true;

			for (FPinMap::TConstIterator Iter = NodeInputs[Node].CreateConstIterator(); Iter; ++Iter)
			{
				const FPin& InputPin = Iter.Value();
				VisitNode(InputPin.Node, Visited, SortedNodes, NodeInputs, NodeOutputs);
			}

			SortedNodes.Push(Node);

			for (FPinMap::TConstIterator Iter = NodeOutputs[Node].CreateConstIterator(); Iter; ++Iter)
			{
				const FPin& OutputPin = Iter.Value();
				VisitNode(OutputPin.Node, Visited, SortedNodes, NodeInputs, NodeOutputs);
			}
		}
	};

	TArray<bool> NodeVisited;
	NodeVisited.SetNumZeroed(Nodes.Num());
	OutOrder.Reset();
	for (const TPair<int32, int32>& Pair : SortedMutableNodes)
	{
		Local::VisitNode(Pair.Value, NodeVisited, OutOrder, NodeInputs, NodeOutputs);
	}
	check(OutOrder.Num() == Nodes.Num());
	return true;
}

int32 FControlRigDAG::GetMaxDistanceToLeafOutput(int32 Node) const
{
	if(NodeOutputs[Node].Num() == 0)
	{
		return 0;
	}

	int32 MaxDistance = 0;
	for (FPinMap::TConstIterator Iter = NodeOutputs[Node].CreateConstIterator(); Iter; ++Iter)
	{
		int32 Distance = GetMaxDistanceToLeafOutput(Iter.Value().Node);
		MaxDistance = FMath::Max<int32>(Distance, MaxDistance);
	}
	return MaxDistance + 1;
}

// Finds the cycles as node index arrays
TArray<int32> FControlRigDAG::FindCycle()
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
bool FControlRigDAG::IsNodeCyclic(int32 Node)
{
	for (const TPair<int32, FPin>& Pair : NodeOutputs[Node])
	{
		int32 Neighbor = Pair.Value.Node;
		if (CycleBlackList.Contains(Neighbor))
		{
			continue;
		}
		else if (CycleWhiteList.Contains(Neighbor))
		{
			CycleDepthTraversal.Add(Neighbor, Node);
			CycleWhiteList.Remove(Neighbor);
			CycleGreyList.Add(Neighbor);
			if(IsNodeCyclic(Neighbor))
			{
				return true;
			}
		}
		else if (CycleGreyList.Contains(Neighbor))
		{
			// this means we've detected a cycle
			while(Node != -1)
			{
				Cycle.Add(Node);
				Node = CycleDepthTraversal.FindChecked(Node);
				if (Node == Neighbor)
				{
					Cycle.Add(Node);
					break;
				}
			}
			return true;
		}
	}

	// if the current node has no neighbors we clear the grey list
	CycleGreyList.Remove(Node);
	CycleBlackList.Add(Node);
	return false;
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
		UE_LOG(LogControlRigDAG, Display, TEXT("DAG.AddLink(%d, %d, %d, %d);"), Link.Key.Node, Link.Value.Node, Link.Key.Order, Link.Value.Order);
	}
}
