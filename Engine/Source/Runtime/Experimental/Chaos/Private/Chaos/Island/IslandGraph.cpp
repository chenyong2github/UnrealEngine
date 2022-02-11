// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Island/IslandGraph.h"
#include "Chaos/Island/SolverIsland.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ConstraintHandle.h"

#include "ChaosStats.h"
#include "ChaosLog.h"

namespace Chaos
{

DECLARE_CYCLE_STAT(TEXT("MergeIslandGraph"), STAT_MergeIslandGraph, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("SplitIslandGraph"), STAT_SplitIslandGraph, STATGROUP_Chaos);

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ResetNodes()
{
	GraphNodes.Reset();
	ItemNodes.Reset();
	GraphIslands.Reset();
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ReserveNodes(const int32 NumNodes)
{
	GraphNodes.Reserve(NumNodes);
	ItemNodes.Reserve(NumNodes);
	GraphIslands.Reserve(NumNodes);
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ParentIslands(const int32 FirstIsland, const int32 SecondIsland, const bool bIsEdgeMoving)
{
	if (GraphIslands.IsValidIndex(FirstIsland) && GraphIslands.IsValidIndex(SecondIsland) && (FirstIsland != SecondIsland))
	{
		GraphIslands[SecondIsland].ChildrenIslands.Add(FirstIsland);
		GraphIslands[FirstIsland].ChildrenIslands.Add(SecondIsland);

		if(bIsEdgeMoving)
		{
			// If we are adding a constraint the island is no longer persistent
			GraphIslands[FirstIsland].bIsPersistent = false;
			GraphIslands[SecondIsland].bIsPersistent = false;
		}
	}
}
	
template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::UpdateNode(const NodeType& NodeItem, const bool bValidNode, const int32 IslandIndex, const bool bStationaryNode, const int32 NodeIndex)
{
	if (GraphNodes.IsValidIndex(NodeIndex))
	{
		FGraphNode& GraphNode = GraphNodes[NodeIndex];
		
		// In case the item is changing its state to be valid
		// we merge all the connected islands 
		if (bValidNode && !GraphNode.bValidNode)
		{
			int32 MasterIsland = INDEX_NONE;
			for (int32& EdgeIndex : GraphNode.NodeEdges)
			{
				FGraphEdge& GraphEdge = GraphEdges[EdgeIndex];
				const bool bIsEdgeMoving = IsEdgeMoving(EdgeIndex);
				
				ParentIslands(MasterIsland, GraphEdge.IslandIndex, bIsEdgeMoving);
				MasterIsland = GraphEdge.IslandIndex;
				if(bIsEdgeMoving)
				{
					GraphIslands[GraphEdge.IslandIndex].bIsPersistent = false;
				}
			}
		}
		// If node is not valid and was valid before we check that all the node edges
		// have at least one valid node. otherwise we remove the edge. 
		if (!bValidNode && GraphNode.bValidNode)
		{
			for (int32 NodeEdge = GraphNode.NodeEdges.GetMaxIndex() - 1; NodeEdge >= 0; --NodeEdge)
			{
				if (GraphNode.NodeEdges.IsValidIndex(NodeEdge))
				{
					const int32 EdgeIndex = GraphNode.NodeEdges[NodeEdge];

					if (GraphEdges.IsValidIndex(EdgeIndex))
					{
						FGraphEdge& GraphEdge = GraphEdges[EdgeIndex];

						// Get the opposite node index for the given edge
						const int32 OtherIndex = (NodeIndex == GraphEdge.FirstNode) ?
							GraphEdge.SecondNode : GraphEdge.FirstNode;

						if (!GraphNodes.IsValidIndex(OtherIndex) || (GraphNodes.IsValidIndex(OtherIndex) && !GraphNodes[OtherIndex].bValidNode))
						{
							RemoveEdge(EdgeIndex);
						}
					}
				}
			}
		}
		GraphNode.bValidNode = bValidNode;
		GraphNode.bStationaryNode = bStationaryNode;
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
int32 FIslandGraph<NodeType, EdgeType, IslandType>::AddNode(const NodeType& NodeItem, const bool bValidNode, const int32 IslandIndex, const bool bStationaryNode)
{
	int32 NodeIndex = INDEX_NONE;
	int32* ItemIndex = ItemNodes.Find(NodeItem);
	if (ItemIndex && GraphNodes.IsValidIndex(*ItemIndex))
	{
		UpdateNode(NodeItem,bValidNode,IslandIndex,bStationaryNode,*ItemIndex);
		NodeIndex = *ItemIndex;
	}
	else
	{
		FGraphNode GraphNode;
		GraphNode.NodeItem = NodeItem;
		GraphNode.IslandIndex = IslandIndex;
		
		GraphNode.bValidNode = bValidNode;
		GraphNode.bStationaryNode = bStationaryNode;

		NodeIndex =  ItemNodes.Add(NodeItem, GraphNodes.Emplace(GraphNode));
	}
	return NodeIndex;
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::RemoveNode(const NodeType& NodeItem)
{
	if (const int32* IndexPtr = ItemNodes.Find(NodeItem))
	{
		const int32 NodeIndex = *IndexPtr;
		if (GraphNodes.IsValidIndex(NodeIndex))
		{
			FGraphNode& GraphNode = GraphNodes[NodeIndex];
			
			// If only one node and zero edges, we invalidate the node island
			if(GraphNode.NodeEdges.Num() == 0  && GraphIslands.IsValidIndex(GraphNode.IslandIndex))
			{
				if(!GraphNode.bStationaryNode)
				{
					GraphIslands[GraphNode.IslandIndex].bIsPersistent = false;
				}
			}
			else
			{
				// Otherwise we loop over all the connected edges to remove the node inside the nodeedges list
				for (int32 NodeEdgeIndex = GraphNode.NodeEdges.GetMaxIndex() - 1; NodeEdgeIndex >= 0; --NodeEdgeIndex)
				{
					if (GraphNode.NodeEdges.IsValidIndex(NodeEdgeIndex))
					{
						const int32 GraphEdgeIndex = GraphNode.NodeEdges[NodeEdgeIndex];
						if(IsEdgeMoving(GraphEdgeIndex))
						{
							GraphIslands[GraphEdges[GraphEdgeIndex].IslandIndex].bIsPersistent = false;
						}
						RemoveEdge(GraphEdgeIndex);
					}
				}
			}
			ItemNodes.Remove(NodeItem);
			GraphNodes.RemoveAt(NodeIndex);
		}
		else
		{
			UE_LOG(LogChaos, Error, TEXT("Island Graph : Trying to remove a node at index %d in a list of size %d"), NodeIndex, GraphNodes.Num());
		}
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ResetEdges()
{
	GraphEdges.Reset();
	ItemEdges.Reset();
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ReserveEdges(const int32 NumEdges)
{
	GraphEdges.Reserve(NumEdges);
	ItemEdges.Reserve(NumEdges);
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::AttachIslands(const int32 EdgeIndex)
{
	if (GraphEdges.IsValidIndex(EdgeIndex))
	{
		FGraphEdge& GraphEdge = GraphEdges[EdgeIndex];

		if (GraphNodes.IsValidIndex(GraphEdge.FirstNode) && GraphNodes.IsValidIndex(GraphEdge.SecondNode))
		{
			FGraphNode& FirstNode = GraphNodes[GraphEdge.FirstNode];
			FGraphNode& SecondNode = GraphNodes[GraphEdge.SecondNode];

			const bool bFirstValidIsland = GraphIslands.IsValidIndex(FirstNode.IslandIndex) && FirstNode.bValidNode;
			const bool bSecondValidIsland = GraphIslands.IsValidIndex(SecondNode.IslandIndex) && SecondNode.bValidNode;

			const bool bIsEdgeMoving = IsEdgeMoving(EdgeIndex);

			// We check if one of the 2 nodes have invalid island
			// if yes we set the invalid node island index 
			// and the edge one to be the valid one 
			// If none are valid we create a new island 
			if (bFirstValidIsland && !bSecondValidIsland)
			{
				GraphEdge.IslandIndex = FirstNode.IslandIndex;
				SecondNode.IslandIndex = GraphEdge.IslandIndex;
				if(SecondNode.bValidNode && bIsEdgeMoving)
				{
					GraphIslands[GraphEdge.IslandIndex].bIsPersistent = false;
				}
			}
			else if (!bFirstValidIsland && bSecondValidIsland)
			{
				GraphEdge.IslandIndex = SecondNode.IslandIndex;
				FirstNode.IslandIndex = GraphEdge.IslandIndex;
				if(FirstNode.bValidNode && bIsEdgeMoving)
				{
					GraphIslands[GraphEdge.IslandIndex].bIsPersistent = false;
				}
			}
			else if (!bFirstValidIsland && !bSecondValidIsland)
			{
				FGraphIsland GraphIsland = { 1, 2 };
				GraphEdge.IslandIndex = GraphIslands.Emplace(GraphIsland);

				// We set both island indices to be the equal to the edge one
				FirstNode.IslandIndex = GraphEdge.IslandIndex;
				SecondNode.IslandIndex = GraphEdge.IslandIndex;
			}
			else
			{
				// If the 2 nodes are coming from 2 different islands, we need to merge these islands
				// In order to do that we build an island graph and we will 
				// merge recursively the children islands onto the parent one
				GraphEdge.IslandIndex = FMath::Min(FirstNode.IslandIndex, SecondNode.IslandIndex);
				ParentIslands(FirstNode.IslandIndex, SecondNode.IslandIndex, bIsEdgeMoving);
			}
		}
		else if (GraphNodes.IsValidIndex(GraphEdge.FirstNode) && !GraphNodes.IsValidIndex(GraphEdge.SecondNode) && GraphNodes[GraphEdge.FirstNode].bValidNode)
		{
			// If only the first node is valid, check if its island index is valid and if yes use it for the edge.
			// otherwise we create a new island
			if(!GraphIslands.IsValidIndex(GraphNodes[GraphEdge.FirstNode].IslandIndex))
			{
				FGraphIsland GraphIsland = { 1, 1 };
				GraphNodes[GraphEdge.FirstNode].IslandIndex =  GraphIslands.Emplace(GraphIsland);
			}
			GraphEdge.IslandIndex =  GraphNodes[GraphEdge.FirstNode].IslandIndex;
		}
		else if (!GraphNodes.IsValidIndex(GraphEdge.FirstNode) && GraphNodes.IsValidIndex(GraphEdge.SecondNode) && GraphNodes[GraphEdge.SecondNode].bValidNode)
		{
			// If only the second node is valid, check if its island index is valid and if yes use it for the edge.
			// otherwise we create a new island
			if(!GraphIslands.IsValidIndex(GraphNodes[GraphEdge.SecondNode].IslandIndex))
			{
				FGraphIsland GraphIsland = { 1, 1 };
				GraphNodes[GraphEdge.SecondNode].IslandIndex =  GraphIslands.Emplace(GraphIsland);
			}
			GraphEdge.IslandIndex =  GraphNodes[GraphEdge.SecondNode].IslandIndex;
		}
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
int32 FIslandGraph<NodeType, EdgeType, IslandType>::AddEdge(const EdgeType& EdgeItem, const int32 ItermContainer, const int32 FirstNode, const int32 SecondNode)
{
	int32 EdgeIndex = INDEX_NONE;
	// We only add an edge if one of the 2 nodes are valid
	if (GraphNodes.IsValidIndex(FirstNode) || GraphNodes.IsValidIndex(SecondNode))
	{
		// This test could be quite slow. will probably be better to use the graph index that is stored on the constraint handle
		int32* ItemIndex = ItemEdges.Find(EdgeItem);
		if (ItemIndex)
		{
			// If the edge is already there, no need to attach islands since it must have been done before
			EdgeIndex = *ItemIndex;
		}
		else
		{
			// Create a  new edge and enqueue the linked islands to be merged if necessary
			FGraphEdge GraphEdge;
			GraphEdge.EdgeItem = EdgeItem;
			GraphEdge.FirstNode = FirstNode;
			GraphEdge.SecondNode = SecondNode;
			GraphEdge.IslandIndex = INDEX_NONE;
			GraphEdge.ItemContainer = ItermContainer;

			EdgeIndex = GraphEdges.Emplace(GraphEdge);
			ItemEdges.Add(EdgeItem, EdgeIndex);

			GraphEdges[EdgeIndex].FirstEdge = GraphNodes.IsValidIndex(FirstNode) ? GraphNodes[FirstNode].NodeEdges.Add(EdgeIndex) : -1;
			GraphEdges[EdgeIndex].SecondEdge = GraphNodes.IsValidIndex(SecondNode) ? GraphNodes[SecondNode].NodeEdges.Add(EdgeIndex) : -1;

			AttachIslands(EdgeIndex);
		}
	}	
	else
	{
		UE_LOG(LogChaos, Error, TEXT("Island Graph : Trying to add an edge with invalid node indices %d  %d in a list of nodes of size %d"), FirstNode, SecondNode, GraphNodes.Num());
	}
	return EdgeIndex;
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::RemoveEdge(const int32 EdgeIndex)
{
	if (GraphEdges.IsValidIndex(EdgeIndex))
	{
		FGraphEdge& GraphEdge = GraphEdges[EdgeIndex];
		// We first remove the edge from the nodes edges list
		if(GraphNodes.IsValidIndex(GraphEdge.FirstNode) && GraphNodes[GraphEdge.FirstNode].NodeEdges.IsValidIndex(GraphEdge.FirstEdge))
		{
			GraphNodes[GraphEdge.FirstNode].NodeEdges.RemoveAt(GraphEdge.FirstEdge);
		}
		if (GraphNodes.IsValidIndex(GraphEdge.SecondNode) && GraphNodes[GraphEdge.SecondNode].NodeEdges.IsValidIndex(GraphEdge.SecondEdge))
		{
			GraphNodes[GraphEdge.SecondNode].NodeEdges.RemoveAt(GraphEdge.SecondEdge);
		}
		// We then remove the edge from the item and graph edges
		ItemEdges.Remove(GraphEdge.EdgeItem);
		GraphEdges.RemoveAt(EdgeIndex);
	}
	else
	{
		UE_LOG(LogChaos, Error, TEXT("Island Graph : Trying to remove an edge at index %d in a list of size %d"), EdgeIndex, GraphEdges.Num());
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::MergeIslands(const int32 ParentIndex, const int32 ChildIndex)
{
	int32 CurrentIndex;
	TQueue<int32> ChildQueue;
	ChildQueue.Enqueue(ChildIndex);
	
	while(!ChildQueue.IsEmpty())
	{
		ChildQueue.Dequeue(CurrentIndex);
	
		if (GraphIslands[CurrentIndex].IslandCounter != GraphCounter && ParentIndex != CurrentIndex)
		{
			GraphIslands[CurrentIndex].IslandCounter = GraphCounter;
			GraphIslands[CurrentIndex].ParentIsland = ParentIndex;
	
			// Add the number of nodes/edges to the parent one and set to 0 the children ones
			GraphIslands[ParentIndex].NumEdges += GraphIslands[CurrentIndex].NumEdges;
			GraphIslands[CurrentIndex].NumEdges = 0;
	
			GraphIslands[ParentIndex].NumNodes += GraphIslands[CurrentIndex].NumNodes;
			GraphIslands[CurrentIndex].NumNodes = 0;
	
			// Recursively iterate over all the children ones
			for (auto& MergedIsland : GraphIslands[CurrentIndex].ChildrenIslands)
			{
				ChildQueue.Enqueue(MergedIsland);
			}
			GraphIslands[CurrentIndex].ChildrenIslands.Reset();
		}
	}
}
	
template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::MergeIslands()
{
	GraphCounter = (GraphCounter + 1) % MaxCount;

	// Init the Parent index to be the island one
	for (int32 IslandIndex = 0, NumIslands = GraphIslands.GetMaxIndex(); IslandIndex < NumIslands; ++IslandIndex)
	{
		if (GraphIslands.IsValidIndex(IslandIndex))
		{
			GraphIslands[IslandIndex].ParentIsland = IslandIndex;
		}
	}

	// We loop over all the islands and if they have children
	// we merge them onto the parent one
	for (int32 IslandIndex = 0, NumIslands = GraphIslands.GetMaxIndex(); IslandIndex < NumIslands; ++IslandIndex)
	{
		if (GraphIslands.IsValidIndex(IslandIndex))
		{
			FGraphIsland& GraphIsland = GraphIslands[IslandIndex];
			// If the graph island is persistent we don't need to merge its children
			// and update the parent index
			if (!GraphIsland.bIsPersistent)
			{
				for (const int32& MergedIsland : GraphIsland.ChildrenIslands)
				{
					MergeIslands(IslandIndex, MergedIsland);
				}
				GraphIsland.ChildrenIslands.Reset();
			}
		}
	}

	// Reassigns all the parent island indices to the nodes/edges
	ReassignIslands();

	// Once the merging process is done we remove all the children islands
	// since they have been merged onto the parent one
	for (int32 IslandIndex = (GraphIslands.GetMaxIndex() - 1); IslandIndex >= 0; --IslandIndex)
	{
		// Only the island counter of the children have been updated
		if (GraphIslands.IsValidIndex(IslandIndex) && GraphIslands[IslandIndex].NumNodes == 0)
		{
			GraphIslands.RemoveAt(IslandIndex);
		}
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::InitSorting()
{
	// Reset nodes levels and colors
	for(auto& GraphNode : GraphNodes)
	{
		GraphNode.LevelIndex = INDEX_NONE;
		GraphNode.ColorIndices.Reset();
	}
	// Reset edges levels and colors
	for(auto& GraphEdge : GraphEdges)
	{
		GraphEdge.LevelIndex = INDEX_NONE;
		GraphEdge.ColorIndex = INDEX_NONE;
	}
	// Reset island max number of levels and colors
	for(auto& GraphIsland : GraphIslands)
	{
		GraphIsland.MaxLevels = INDEX_NONE;
		GraphIsland.MaxColors = INDEX_NONE;
	}
}
	
template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::UpdateLevels(const int32 NodeIndex, const int32 ContainerId, TQueue<int32>& NodeQueue)
{
	if(GraphNodes.IsValidIndex(NodeIndex))
	{
		FGraphNode& GraphNode = GraphNodes[NodeIndex];
		for (int32 EdgeIndex : GraphNode.NodeEdges)
		{
			FGraphEdge& GraphEdge = GraphEdges[EdgeIndex];

			// Do nothing if the edge is not coming from the same container or if the island is sleeping 
			if (GraphEdge.ItemContainer == ContainerId && GraphEdge.LevelIndex == INDEX_NONE && !GraphIslands[GraphEdge.IslandIndex].bIsSleeping)
			{
				const int32 OtherIndex = (NodeIndex == GraphEdge.FirstNode) ?
							GraphEdge.SecondNode : GraphEdge.FirstNode;

				GraphEdge.LevelIndex = GraphNode.LevelIndex;
					
				GraphIslands[GraphEdge.IslandIndex].MaxLevels = FGenericPlatformMath::Max(
					GraphIslands[GraphEdge.IslandIndex].MaxLevels, GraphEdge.LevelIndex);

				// If we have another node, append it to our queue on the next level
				if (GraphNodes.IsValidIndex(OtherIndex) && GraphNodes[OtherIndex].bValidNode && GraphNodes[OtherIndex].LevelIndex == INDEX_NONE)
				{
					GraphNodes[OtherIndex].LevelIndex = GraphEdge.LevelIndex+1;
					NodeQueue.Enqueue(OtherIndex);
				}
			}
		}
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ComputeLevels(const int32 ContainerId)
{
	// First enqueue all the static/kinematic nodes for levels 0
	TQueue<int32> NodeQueue;
	for (int32 NodeIndex = 0, NumNodes = GraphNodes.GetMaxIndex(); NodeIndex < NumNodes; ++NodeIndex)
	{
		if(GraphNodes.IsValidIndex(NodeIndex))
		{
			FGraphNode& GraphNode = GraphNodes[NodeIndex];
			if(!GraphNode.bValidNode)
			{
				GraphNode.LevelIndex = 0;
				UpdateLevels(NodeIndex, ContainerId, NodeQueue);
			}
		}
	}

	// Then iteratively loop over these root nodes and propagate the levels through connectivity
	int32 NodeIndex = INDEX_NONE;
	while(!NodeQueue.IsEmpty())
	{
		NodeQueue.Dequeue(NodeIndex);
		UpdateLevels(NodeIndex, ContainerId, NodeQueue);
	}

	// An isolated island that is only dynamics will not have been processed above, put everything without a level into level zero
	for(auto& GraphEdge : GraphEdges)
	{
		if(GraphEdge.ItemContainer == ContainerId)
		{
			GraphEdge.LevelIndex = FGenericPlatformMath::Max(GraphEdge.LevelIndex, 0);
		}
	}
}
	
template<typename NodeType, typename EdgeType, typename IslandType>
int32 FIslandGraph<NodeType, EdgeType, IslandType>::PickColor(const FGraphNode& GraphNode, const int32 OtherIndex, TQueue<int32>& NodeQueue)
{
	int32 ColorToUse = 0;
	if (GraphNodes.IsValidIndex(OtherIndex) && GraphNodes[OtherIndex].bValidNode)
	{
		FGraphNode& OtherNode = GraphNodes[OtherIndex];
		// Pick the first color not used by the 2 edge nodes
		while (OtherNode.ColorIndices.Contains(ColorToUse) || GraphNode.ColorIndices.Contains(ColorToUse))
		{
			ColorToUse++;
		}
		// The color will be added to the graph node in the UpdateColors function
		OtherNode.ColorIndices.Add(ColorToUse);
		if (OtherNode.NodeCounter != GraphCounter)
		{
			NodeQueue.Enqueue(OtherIndex);
		}
	}
	else
	{
		// If only one node, only iterate over that node available color
		while (GraphNode.ColorIndices.Contains(ColorToUse))
		{
			ColorToUse++;
		}
	}
	return ColorToUse;
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::UpdateColors(const int32 NodeIndex, const int32 ContainerId, TQueue<int32>& NodeQueue, const int32 MinEdges)
{
	if (GraphNodes.IsValidIndex(NodeIndex))
	{
		FGraphNode& GraphNode = GraphNodes[NodeIndex];
		GraphNode.NodeCounter = GraphCounter;
		
		for (int32 EdgeIndex : GraphNode.NodeEdges)
		{
			FGraphEdge& GraphEdge = GraphEdges[EdgeIndex];

			// Do nothing if the edge is not coming from the same container or if the island is sleeping 
			if (GraphEdge.ItemContainer == ContainerId && GraphEdge.ColorIndex == INDEX_NONE &&
				!GraphIslands[GraphEdge.IslandIndex].bIsSleeping && (GraphIslands[GraphEdge.IslandIndex].NumEdges > MinEdges) )
			{
				// Get the opposite node index for the given edge
				const int32 OtherIndex = (NodeIndex == GraphEdge.FirstNode) ?
					GraphEdge.SecondNode : GraphEdge.FirstNode;

				// Get the first available color to be used by the edge
				const int32 ColorToUse = PickColor(GraphNode, OtherIndex, NodeQueue);
				
				GraphNode.ColorIndices.Add(ColorToUse);
				GraphEdge.ColorIndex = ColorToUse;
				
				GraphIslands[GraphEdge.IslandIndex].MaxColors = FGenericPlatformMath::Max(
					GraphIslands[GraphEdge.IslandIndex].MaxColors, ColorToUse);
			}
		}
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ComputeColors(const int32 ContainerId, const int32 MinEdges)
{
	GraphCounter = (GraphCounter + 1) % MaxCount;

	TQueue<int32> NodeQueue;
	int32 NodeIndex = INDEX_NONE;

	// We first loop over all the nodes that have not been processed and valid (dynamic/sleeping)
	for (int32 RootIndex = 0, NumNodes = GraphNodes.GetMaxIndex(); RootIndex < NumNodes; ++RootIndex)
	{
		if (GraphNodes.IsValidIndex(RootIndex))
		{
			FGraphNode& GraphNode = GraphNodes[RootIndex];
			if (GraphNode.NodeCounter != GraphCounter && GraphNode.bValidNode)
			{
				NodeQueue.Enqueue(RootIndex);
				while (!NodeQueue.IsEmpty())
				{
					NodeQueue.Dequeue(NodeIndex);
					UpdateColors(NodeIndex, ContainerId, NodeQueue, MinEdges);
				}
			}
		}
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::SplitIsland(TQueue<int32>& NodeQueue, const int32 RootIndex, const int32 IslandIndex)
{
	NodeQueue.Enqueue(RootIndex);
	int32 NodeIndex = RootIndex;
					
	while (!NodeQueue.IsEmpty())
	{
		NodeQueue.Dequeue(NodeIndex);
		FGraphNode& GraphNode = GraphNodes[NodeIndex];
						
		// Graph counter is there to avoid processing multiple times the same node/edge
		if (GraphNode.NodeCounter != GraphCounter)
		{
			GraphNode.NodeCounter = GraphCounter;
			GraphNode.IslandIndex = IslandIndex;

			// Loop over the node edges to continue the island discovery
			for (int32& EdgeIndex : GraphNode.NodeEdges)
			{
				FGraphEdge& GraphEdge = GraphEdges[EdgeIndex];
				if(GraphEdge.EdgeCounter != GraphCounter)
				{
					GraphEdge.EdgeCounter = GraphCounter;
					GraphEdge.IslandIndex = IslandIndex;
				}
				const int32 OtherIndex = (NodeIndex == GraphEdge.FirstNode) ?
					GraphEdge.SecondNode : GraphEdge.FirstNode;

				// Only the valid nodes are allowed to continue the graph traversal
				if (GraphNodes.IsValidIndex(OtherIndex) && GraphNodes[OtherIndex].NodeCounter != GraphCounter && GraphNodes[OtherIndex].bValidNode)
				{
					NodeQueue.Enqueue(OtherIndex);
				}
			}
		}
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::SplitIslands()
{
	SCOPE_CYCLE_COUNTER(STAT_SplitIslandGraph);

	GraphCounter = (GraphCounter + 1) % MaxCount;
	TQueue<int32> NodeQueue;
	for (int32 RootIndex = 0, NumNodes = GraphNodes.GetMaxIndex(); RootIndex < NumNodes; ++RootIndex)
	{
		// We pick all the nodes that are inside an island
		if(GraphNodes.IsValidIndex(RootIndex))
		{
			FGraphNode& RootNode = GraphNodes[RootIndex];
			if(RootNode.NodeCounter != GraphCounter)
			{
				if(RootNode.bValidNode)
				{
					int32 CurrentIsland = RootNode.IslandIndex;
					
					if (GraphIslands.IsValidIndex(CurrentIsland) && GraphIslands[CurrentIsland].bIsPersistent && !GraphIslands[CurrentIsland].bIsSleeping)
					{
						// We don't want to rebuild a new island if this one can't be splitted
						// It is why by default the first one is the main one
						if(GraphIslands[CurrentIsland].IslandCounter == GraphCounter)
						{
							FGraphIsland GraphIsland = { 0, 1, 0, false, false };
							CurrentIsland = GraphIslands.Emplace(GraphIsland);
						}
						
						GraphIslands[CurrentIsland].IslandCounter = GraphCounter;

						SplitIsland(NodeQueue, RootIndex, CurrentIsland);
					}
				}
				else
				{
					RootNode.IslandIndex = INDEX_NONE;
				}
			}
		}
	}
}
	
template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ReassignIslands()
{
	// Update all the nodes indices
	for (FGraphIsland& GraphIsland : GraphIslands)
	{
		GraphIsland.NumNodes = 0;
		GraphIsland.NumEdges = 0;
	}
	
	// Update all the edges indices
	for (FGraphEdge& GraphEdge : GraphEdges)
	{
		if(GraphIslands.IsValidIndex(GraphEdge.IslandIndex))
		{
			const int32 ParentIndex = GraphIslands[GraphEdge.IslandIndex].ParentIsland;
			if(GraphIslands.IsValidIndex(ParentIndex))
			{
				GraphEdge.IslandIndex = ParentIndex;
				GraphIslands[ParentIndex].NumEdges++;
			}
		}
	}

	// Update all the nodes indices
	for (FGraphNode& GraphNode : GraphNodes)
	{
		if(GraphIslands.IsValidIndex(GraphNode.IslandIndex))
		{
			const int32 ParentIndex = GraphIslands[GraphNode.IslandIndex].ParentIsland;
			if(GraphIslands.IsValidIndex(ParentIndex))
			{
				GraphNode.IslandIndex = ParentIndex;
				if(GraphNode.bValidNode)
				{
					GraphIslands[ParentIndex].NumNodes++;
				}
			}
		}
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::UpdateGraph()
{
	SCOPE_CYCLE_COUNTER(STAT_MergeIslandGraph);

	// Merge all the islands if necessary
	MergeIslands();

	// Add all the single particle islands and update the sleeping flag
	for (int32 NodeIndex = 0, NumNodes = GraphNodes.GetMaxIndex(); NodeIndex < NumNodes; ++NodeIndex)
	{
		if (GraphNodes.IsValidIndex(NodeIndex))
		{
			FGraphNode& GraphNode = GraphNodes[NodeIndex];
			// Add new islands for the all the particles that are not connected into the graph 
			if(!GraphIslands.IsValidIndex(GraphNode.IslandIndex) && GraphNode.bValidNode)
			{
				FGraphIsland GraphIsland = { 0, 1 };
				GraphNode.IslandIndex = GraphIslands.Emplace(GraphIsland);
			}
			// Update of the sleeping flag on the island
			if(GraphIslands.IsValidIndex(GraphNode.IslandIndex) && !GraphNode.bStationaryNode)
			{
				if(GraphNode.NodeEdges.Num() == 0)
				{
					GraphIslands[GraphNode.IslandIndex].bIsSleeping = false;
				}
				else
				{
					// if a node is a moving kinematic and is switching its stationary state
					// we have to iterate through all the edges since the kinematic node could belong to several islands
					for (auto& EdgeIndex : GraphNode.NodeEdges)
					{
						GraphIslands[GraphEdges[EdgeIndex].IslandIndex].bIsSleeping = false;
					}
				}
			}
		}
	}

	// Set the sleeping flag to false if the persistent flag is not set
	for (FGraphIsland& GraphIsland : GraphIslands)
	{
		if (!GraphIsland.bIsPersistent)
		{
			GraphIsland.bIsSleeping = false;
		}
	}
	
	// Split the islands if this one is persistent and not sleeping
	SplitIslands();
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::InitIslands()
{
	// Remove of all the non sleeping edges
	for(int32 EdgeIndex = GraphEdges.GetMaxIndex(); EdgeIndex >= 0; --EdgeIndex)
	{
		if(GraphEdges.IsValidIndex(EdgeIndex) && GraphIslands.IsValidIndex(GraphEdges[EdgeIndex].IslandIndex) && !GraphIslands[GraphEdges[EdgeIndex].IslandIndex].bIsSleeping)
		{
			RemoveEdge(EdgeIndex);
		}
	} 

	// Reset of the sleeping flag for the graph islands
	for (auto& GraphIsland : GraphIslands)
	{
		GraphIsland.bIsSleeping = true;
	}
}

template<typename NodeType, typename EdgeType, typename IslandType>
void FIslandGraph<NodeType, EdgeType, IslandType>::ResetIslands()
{
	GraphEdges.Reset();
	ItemEdges.Reset();
	
	// Reset of all the edges + the nodes ones
	// will probably need to change that with persistent contacts
	for (int32 NodeIndex = 0, NumNodes = GraphNodes.GetMaxIndex(); NodeIndex < NumNodes; ++NodeIndex)
	{
		if (GraphNodes.IsValidIndex(NodeIndex))
		{
			GraphNodes[NodeIndex].NodeEdges.Reset();
		}
	}
}

template class FIslandGraph<FGeometryParticleHandle*, FConstraintHandleHolder, FPBDIslandSolver*>;
template class FIslandGraph<int32, int32, int32>;
}
