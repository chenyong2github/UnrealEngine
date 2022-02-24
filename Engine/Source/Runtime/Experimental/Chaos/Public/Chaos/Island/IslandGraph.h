// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Containers/SparseArray.h"

namespace Chaos
{

/**
 * Island Graph.
 *
 * This template implements a graph that will be stored on the island manager
 * The goal here is to minimize memory allocation while doing graph operations. 
 *
 * @param NodeType The node type of the item that will be stored in each nodes
 * @param EdgeType The edge type of the item that will be stored in each edges
 */
template<typename NodeType, typename EdgeType, typename IslandType>
class CHAOS_API FIslandGraph
{
public:

	static constexpr const int32 MaxCount = 100000;

	/**
	 * Update current graph to merge connected islands 
	 */
	void UpdateGraph();

	/**
	 * Reserve a number of nodes in memory before adding them later
	 * @param NumNodes Number of nodes to be reserved in memory
	 */
	void ReserveNodes(const int32 NumNodes);

	/**
	 * Clear the graph nodes without deallocating memory
	 */
	void ResetNodes();

	/**
	 * Given a node item, add a node to the graph nodes
	 * @param NodeItem Node Item to be stored in the graph node
	 * @param bValidNode Check if a node should be considered for graph partitioning
	 * @param IslandIndex Potential island index we want the node to belong to
	 * @param bStationaryNode Boolean to check if a node is steady or not
	 * @return Node index that has just been added
	 */
	int32 AddNode(const NodeType& NodeItem, const bool bValidNode = true, const int32 IslandIndex = INDEX_NONE, const bool bStationaryNode = false);

	/**
	* Given a node item, update the graph node information (valid,discard...)
	* @param NodeItem Node Item to be stored in the graph node
	* @param bValidNode Check if a node should be considered for graph partitioning
	* @param IslandIndex Potential island index we want the node to belong to
	* @param bStationaryNode Boolean to check if a node is steady or not
	*  @param NodeIndex Index to consider to update the graph node information
	* @return Node index that has just been added
	*/
	void UpdateNode(const NodeType& NodeItem, const bool bValidNode, const int32 IslandIndex, const bool bStationaryNode, const int32 NodeIndex);

	/**
	 * Remove a node from the graph nodes list
	 * @param NodeItem Item to be removed from the nodes list
	 */
	void RemoveNode(const NodeType& NodeItem);

	/**
	 * Reserve a number of edges in memory before adding them later
	 * @param NumEdges Number of edges to be reserved in memory
	 */
	void ReserveEdges(const int32 NumEdges);

	/**
	 * Reset the graph edges without deallocating memory
	 */
	void ResetEdges();

	/**
	 * Given an edge item, add an edge to the graph edges
	 * @param EdgeItem Node Item to be stored in the graph edge
	 * @param ItemContainer Item container id that will be stored on the graph edge
	 * @param FirstNode First node index of the graph edge
	 * @param SecondNode Second node index of the graph edge
	 * @return Edge index that has just been added
	 */
	int32 AddEdge(const EdgeType& EdgeItem, const int32 ItemContainer, const int32 FirstNode,  const int32 SecondNode);

	/**
	 * Remove an edge from the graph edges list
	 * @param EdgeIndex Index of the edge to be removed from the edges list
	 */
	void RemoveEdge(const int32 EdgeIndex);

	/**
	* Remove all the edges from the graph
	*/
	void ResetIslands();

	/**
	* Init the islands (remove edges only for non sleeping islands)
	*/
	void InitIslands();

	/**
	 * Get the number of edges that are inside a graph
	 * @return Number of graph edges
	 */
	FORCEINLINE int32 NumEdges() const { return GraphEdges.GetMaxIndex(); }

	/**
	 * Get the number of nodes that are inside a graph
	 * @return Number of graph nodes
	 */
	FORCEINLINE int32 NumNodes() const { return GraphNodes.GetMaxIndex(); }

	/**
	 * Get the number of islands that are inside a graph
	 * @return Number of graph islands
	 */
	FORCEINLINE int32 NumIslands() const { return GraphIslands.GetMaxIndex(); }

	/** Graph node structure */
	struct FGraphNode 
	{
		/** List of edges connected to the node */
		TSparseArray<int32> NodeEdges;

		/** Node Island Index (for static/kinematic particles could belong to several islands) */
		int32 IslandIndex = INDEX_NONE;
		
		/** List of islands in which the node is referenced */
		TSet<int32> NodeIslands;

		/** Check if a node is valid (checked for graph partitioning) */
		bool bValidNode = true;

		/** Check if a node is steady */
		bool bStationaryNode = true;

		/** Node counter to filter nodes already processed */
		int32 NodeCounter = 0;

		/** Node item that is stored per node */
		NodeType NodeItem;
		
		/** Node level index */
		int32 LevelIndex = INDEX_NONE;

		/** Set of used colors */
		TSet<int32> ColorIndices;
	};

	/** Graph edge structure */
	struct FGraphEdge 
	{
		/** First node of the edge */
		int32 FirstNode = INDEX_NONE;

		/** Second node of the edge*/
		int32 SecondNode = INDEX_NONE;

		/** Current edge index in the list of edges of the first node */
		int32 FirstEdge = INDEX_NONE;

		/** Current edge index in the list of edges of the second node  */
		int32 SecondEdge = INDEX_NONE;

		/** Unique edge island index */
		int32 IslandIndex = INDEX_NONE;

		/** Edge counter to filter edges already processed */
		int32 EdgeCounter = 0;

		/** Edge item that is stored per node */
		EdgeType EdgeItem;

		/** Item Container Id  */
		int32 ItemContainer = 0;

		/** Edge level index */
		int32 LevelIndex = INDEX_NONE;

		/** Edge Color index */
		int32 ColorIndex = INDEX_NONE;
	};

	/** Graph island structure */
	struct FGraphIsland
	{
		/** Number of edges per islands*/
		int32 NumEdges = 0;

		/** Number of valid nodes per islands (should be less than the ones in solver islands since it is only including the valid ones)*/
		int32 NumNodes = 0;

		/** Island counter to filter islands already processed */
		int32 IslandCounter = 0;
		
		/** Boolean to check if an island is persistent or not */
		bool bIsPersistent = true;

		/** Boolean to check if an island is sleeping or not */
		bool bIsSleeping = true;

		/** List of children islands  to be merged */
		TSet<int32> ChildrenIslands;

		/** Parent Island */
		int32 ParentIsland = INDEX_NONE;
		
		/** Island Item that is stored per island*/
		IslandType IslandItem;

		/** Max levels per island */
		int32 MaxLevels = INDEX_NONE;

		/** Max color per island */
		int32 MaxColors = INDEX_NONE;
	};

	/**
	 * Link two islands to each other for future island traversal
	 * @param FirstIsland First island index to be linked
	 * @param SecondIsland Second island index to be linked
	 * @param bNonStationary Boolean to check if at least one of the 2 nodes are non stationary
	 */
	void ParentIslands(const int32 FirstIsland, const int32 SecondIsland,const bool bNonStationary);

	/**
	 * Given an edge index we try to attach islands. 
	 * @param EdgeIndex Index of the edge used to attach the islands
	 */
	void AttachIslands(const int32 EdgeIndex);

	/**
	 * Merge 2 islands
	 * @param ParentIndex Index of the parent island
	 * @param ChildIndex Index of the child island
	 */
	void MergeIslands(const int32 ParentIndex, const int32 ChildIndex);

	/**
	* Merge all the graph islands
	*/
	void MergeIslands();

	/**
	* Split one island given a root index 
	* @param NodeQueue node queue to use while splitting the island (avoid reallocation)
	* @param RootIndex root index of the island graph
	* @param IslandIndex index of the island we are splitting
	*/
	void SplitIsland(TQueue<int32>& NodeQueue, const int32 RootIndex, const int32 IslandIndex);

	/**
	* Split all the graph islands if not sleeping
	*/
	void SplitIslands();

	/**
	* Reassign the updated island index from the merging phase to the nodes/edges
	*/
	void ReassignIslands();

	/**
	* Init all the islands/edges/nodes levels and colors for sorting
	*/
	void InitSorting();

	/**
	* Given a constraint containerId compute all the islands/edges/nodes levels
	* @param ContainerId Constraint container id that will be compared to the item container
	*/
	void ComputeLevels(const int32 ContainerId);
	
	/**
	* Given a constraint containerId and a node index update all the islands/edges/nodes levels and push the connected nodes
	* into the node queue
	* @param NodeIndex Node index that we are currently iterating over
	* @param ContainerId Constraint container id that will be compared to the item container
	* @param NodeQueue Node queue in which all the connected nodes not processed yet will be added
	*/
	void UpdateLevels(const int32 NodeIndex, const int32 ContainerId, TQueue<int32>& NodeQueue);

	/**
	* Given a constraint containerId compute all the islands/edges/nodes colors
	* @param ContainerId Constraint container id that will be compared to the item container
	* @param MinEdges Minimum number of edges to compute coloring
	*/
	void ComputeColors(const int32 ContainerId, const int32 MinEdges);

	/**
	* Given a constraint containerId and a node index update all the islands/edges/nodes colors and push the connected nodes
	* into the node queue
	* @param NodeIndex Node index that we are currently iterating over
	* @param ContainerId Constraint container id that will be compared to the item container
	* @param NodeQueue Node queue in which all the connected nodes not processed yet will be added
	* @param MinEdges Minimum number of edges to compute coloring
	*/
	void UpdateColors(const int32 NodeIndex, const int32 ContainerId, TQueue<int32>& NodeQueue, const int32 MinEdges);

	/**
	* Pick the first available color that is not used yet by the current graph node and the edge opposite one
	* @param GraphNode Graph node that we currently iterate over
	* @param OtherIndex Index of the edge opposite node
	* @param NodeQueue Node queue in which all the connected nodes not processed yet will be added
	* @return First available color 
	*/
	int32 PickColor(const FGraphNode& GraphNode, const int32 OtherIndex, TQueue<int32>& NodeQueue);

	/**
	* Check if at least one of the 2 edge nodes is moving
	* @param EdgeIndex Index of the edge to check
	* @return Boolean to check if the edge is moving or not
	*/
	bool IsEdgeMoving(const int32 EdgeIndex)
	{
		if(GraphEdges.IsValidIndex(EdgeIndex))
		{
			const FGraphEdge& GraphEdge = GraphEdges[EdgeIndex];
			if(GraphNodes.IsValidIndex(GraphEdge.FirstNode) && GraphNodes.IsValidIndex(GraphEdge.SecondNode))
			{
				return !GraphNodes[GraphEdge.FirstNode].bStationaryNode || !GraphNodes[GraphEdge.SecondNode].bStationaryNode;
			}
		}
		return false;
	}
	
	/** List of graph nodes */
	TSparseArray<FGraphNode> GraphNodes;

	/** List of graph edges */
	TSparseArray<FGraphEdge> GraphEdges;

	/** List of graph islands */
	TSparseArray<FGraphIsland> GraphIslands;

	/** Reverse list to find given an item the matching node */
	TMap<NodeType,int32> ItemNodes;

	/** Reverse list to find given an item the matching edge */
	TMap<EdgeType,int32> ItemEdges;

	/** Graph counter used for graph traversal to check if an edge/node/island has already been processed */
	int32 GraphCounter = 0;
};

}