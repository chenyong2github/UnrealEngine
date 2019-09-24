// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMGraphUtils.h"
#include "RigVMModel/RigVMGraph.h"

FRigVMGraphUtils::FRigVMGraphUtils()
	: Graph(nullptr)
	, LastCycleCheckingPin(nullptr)
	, LastCycleCheckingPinIsInput(true)
{
}

FRigVMGraphUtils::FRigVMGraphUtils(URigVMGraph* InGraph, URigVMPin* InCycleCheckingPin, bool InCycleCheckingPinIsInput)
	: Graph(InGraph)
	, LastCycleCheckingPin(InCycleCheckingPin)
	, LastCycleCheckingPinIsInput(InCycleCheckingPinIsInput)
{
	ModifiedHandle = Graph->OnModified().AddLambda([this](ERigVMGraphNotifType NotifType, URigVMGraph*, UObject*) {

		switch (NotifType)
		{
			case ERigVMGraphNotifType::GraphChanged:
			case ERigVMGraphNotifType::NodeAdded:
			case ERigVMGraphNotifType::NodeRemoved:
			case ERigVMGraphNotifType::LinkAdded:
			case ERigVMGraphNotifType::LinkRemoved:
			case ERigVMGraphNotifType::PinDirectionChanged:
			case ERigVMGraphNotifType::PinTypeChanged:
			{
				Reset();
				break;
			}
			default:
			{
				break;
			}
		}

	});
}

FRigVMGraphUtils::~FRigVMGraphUtils()
{
	if (Graph && ModifiedHandle.IsValid())
	{
		Graph->OnModified().Remove(ModifiedHandle);
	}
}

void FRigVMGraphUtils::Reset()
{
	CycleWhiteList.Empty();
	CycleGrayList.Empty();
	CycleBlackList.Empty();
	CycleDepthTraversal.Empty();
	Cycle.Empty();

	const TArray<URigVMNode*> Nodes = Graph->GetNodes();
	CycleWhiteList.Reserve(Nodes.Num());
	CycleGrayList.Reserve(Nodes.Num());
	CycleBlackList.Reserve(Nodes.Num());

	LastCycleCheckingPin = nullptr;
	NodeIsOnCycle.Reset();
	NodeIsOnCycle.Reserve(Nodes.Num());
}

bool FRigVMGraphUtils::TopologicalSort(TArray<URigVMNode*>& OutOrder, TArray<URigVMNode*>& OutPotentialCycle)
{
	OutPotentialCycle = FindCycle();
	if (OutPotentialCycle.Num() > 0)
	{
		return false;
	}

	struct Local
	{
		static void VisitNode(URigVMNode* Node, TArray<bool>& Visited, TArray<URigVMNode*>& SortedNodes)
		{
			int32 Index = Node->GetNodeIndex();
			if (Visited[Index])
			{
				return;
			}

			Visited[Index] = true;

			TArray<URigVMNode*> Inputs = Node->GetLinkedSourceNodes();
			for (URigVMNode* Input : Inputs)
			{
				VisitNode(Input, Visited, SortedNodes);
			}

			SortedNodes.Push(Node);

			if (Node->IsMutable())
			{
				TArray<URigVMNode*> Outputs = Node->GetLinkedTargetNodes();
				for (URigVMNode* Output : Outputs)
				{
					VisitNode(Output, Visited, SortedNodes);
				}
			}
		}
	};

	const TArray<URigVMNode*> Nodes = Graph->GetNodes();

	// find all of the left mutable nodes on the left
	TArray<URigVMNode*> LeafNodes;
	TArray<URigVMNode*> OutputParameterNodes;
	for (URigVMNode* Node : Nodes)
	{
		if (Node->IsMutable())
		{
			bool bHasInputPins = false;
			for (URigVMPin* Pin : Node->GetPins())
			{
				if (Pin->GetDirection() == ERigVMPinDirection::Input || Pin->GetDirection() == ERigVMPinDirection::IO)
				{
					bHasInputPins = true;
					break;
				}
			}
			if (!bHasInputPins)
			{
				LeafNodes.Add(Node);
			}
		}
		else if (Node->ContributesToResult())
		{
			OutputParameterNodes.Add(Node);
		}
	}

	LeafNodes.Append(OutputParameterNodes);

	TArray<bool> NodeVisited;
	NodeVisited.SetNumZeroed(Nodes.Num());
	OutOrder.Reset();
	for (URigVMNode* LeafNode : LeafNodes)
	{
		Local::VisitNode(LeafNode, NodeVisited, OutOrder);
	}
	return true;
}

int32 FRigVMGraphUtils::GetMaxDistanceToLeafOutput(URigVMNode* InNode) const
{
	check(Graph)

	TArray<URigVMNode*> Outputs = InNode->GetLinkedTargetNodes();
	if(Outputs.Num() == 0)
	{
		return 0;
	}

	int32 MaxDistance = 0;
	for (URigVMNode* TargetNode : Outputs)
	{
		int32 Distance = GetMaxDistanceToLeafOutput(TargetNode);
		MaxDistance = FMath::Max<int32>(Distance, MaxDistance);
	}
	return MaxDistance + 1;
}

// Finds the cycles as node index arrays
TArray<URigVMNode*> FRigVMGraphUtils::FindCycle()
{
	check(Graph)

	const TArray<URigVMNode*> Nodes = Graph->GetNodes();

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
			CycleGrayList.Add(Node);
			break;
		}

		if(IsNodeCyclic(Node))
		{
			break;
		}
	}

	return Cycle;
}

void FRigVMGraphUtils::PrepareCycleChecking(URigVMPin* InCycleCheckingPin, bool InCycleCheckingPinIsInput)
{
	check(Graph)
		
	if (LastCycleCheckingPin == InCycleCheckingPin && LastCycleCheckingPinIsInput == InCycleCheckingPinIsInput)
	{
		return;
	}

	LastCycleCheckingPin = InCycleCheckingPin;
	LastCycleCheckingPinIsInput = InCycleCheckingPinIsInput;

	check(LastCycleCheckingPin)

	const TArray<URigVMNode*> Nodes = Graph->GetNodes();
	NodeIsOnCycle.SetNumZeroed(Nodes.Num());

	struct Local
	{
		static void VisitNode(URigVMNode* Node, const TArray<URigVMNode*>& Nodes, TArray<bool>& Visited, bool bWalkInputs)
		{
			int32 Index = Node->GetNodeIndex();
			if (Visited[Index])
			{
				return;
			}

			Visited[Index] = true;

			TArray<URigVMNode*> LinkedNodes = bWalkInputs ? Node->GetLinkedSourceNodes() : Node->GetLinkedTargetNodes();
			for (URigVMNode* LinkedNode : LinkedNodes)
			{
				VisitNode(LinkedNode, Nodes, Visited, bWalkInputs);
			}
		}
	};

	Local::VisitNode(LastCycleCheckingPin->GetNode(), Nodes, NodeIsOnCycle, !LastCycleCheckingPinIsInput);
}

bool FRigVMGraphUtils::IsNodeOnCycle(URigVMNode* InNode)
{
	check(LastCycleCheckingPin)
	ensure(NodeIsOnCycle.Num() == Graph->GetNodes().Num());

	struct Local
	{
		static bool TestNodeOnCycle(URigVMNode* Node, const TArray<URigVMNode*>& Nodes, TArray<bool>& Visited, bool bWalkInputs)
		{
			int32 Index = Node->GetNodeIndex();
			if (Visited[Index])
			{
				return true;
			}

			TArray<URigVMNode*> LinkedNodes = bWalkInputs ? Node->GetLinkedSourceNodes() : Node->GetLinkedTargetNodes();
			for (URigVMNode* LinkedNode : LinkedNodes)
			{
				if (TestNodeOnCycle(LinkedNode, Nodes, Visited, bWalkInputs))
				{
					Visited[Index] = true;
					return true;
				}
			}

			return false;
		}
	};

	const TArray<URigVMNode*> Nodes = Graph->GetNodes();
	return Local::TestNodeOnCycle(InNode, Nodes, NodeIsOnCycle, LastCycleCheckingPinIsInput);
}

// this performs a depth first traversal by walking the output
// links of each node
bool FRigVMGraphUtils::IsNodeCyclic(int32 NodeIndex)
{
	if (Graph == nullptr)
	{
		return false;
	}

	const TArray<URigVMNode*> Nodes = Graph->GetNodes();
	URigVMNode* Node = Graph->GetNodes()[NodeIndex];

	TArray<URigVMNode*> Outputs = Node->GetLinkedTargetNodes();
	for (URigVMNode* Output : Outputs)
	{
		int32 NeighborIndex = Output->GetNodeIndex();
		if (CycleBlackList.Contains(NeighborIndex))
		{
			continue;
		}
		else if (CycleWhiteList.Contains(NeighborIndex))
		{
			CycleDepthTraversal.Add(NeighborIndex, NodeIndex);
			CycleWhiteList.Remove(NeighborIndex);
			CycleGrayList.Add(NeighborIndex);
			if(IsNodeCyclic(NeighborIndex))
			{
				return true;
			}
		}
		else if (CycleGrayList.Contains(NeighborIndex))
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

	// if the current node has no neighbors we clear the gray list
	CycleGrayList.Remove(NodeIndex);
	CycleBlackList.Add(NodeIndex);
	return false;
}

