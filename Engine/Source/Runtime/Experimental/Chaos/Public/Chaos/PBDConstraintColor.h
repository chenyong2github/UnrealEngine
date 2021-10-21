// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	class FPBDConstraintGraph;
	class FConstraintHandle;

	/**
	 * Generates color information for a single constraint rule in a connection graph.
	 * Edges with the same color are non-interacting and can safely be processed in parallel.
	 */
	class CHAOS_API FPBDConstraintColor
	{
	public:
		typedef TSet<int32> FColorSet;
		typedef TArray<FConstraintHandle*> FConstraintList;
		typedef TMap<int32, FConstraintList> FColorToConstraintListMap;
		typedef TArray<FColorToConstraintListMap> FLevelToColorToConstraintListMap;

		FPBDConstraintColor()
			: bUseContactGraph(true)
		{}

		int32 NumIslands() const
		{
			return IslandData.Num();
		}

		int32 NumIslandEdges(const int32 Island) const
		{
			return IslandData[Island].NumEdges;
		}

		/**
		 * Initialize the color structures based on the connectivity graph (i.e., reset all color-related node, edge and island data).
		 */
		void InitializeColor(const FPBDConstraintGraph& ConstraintGraph);

		/**
		 * Calculate the color information for the specified island.
		 */
		void ComputeColor(const FReal Dt, const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId);

		/**
		 * Get the Level-Color-ConstraintList map for the specified island.
		 */
		const FLevelToColorToConstraintListMap& GetIslandLevelToColorToConstraintListMap(int32 Island) const;

		/**
		 * Get the maximum color index used in the specified island.
		 */
		int GetIslandMaxColor(int32 Island) const;

		/**
		 * Get the maximum level index used in the specified island.
		 */
		int GetIslandMaxLevel(int32 Island) const;

		void SetUseContactGraph(bool bInUseContactGraph)
		{
			bUseContactGraph = bInUseContactGraph;
		}

#if !UE_BUILD_SHIPPING
		// For debugging onlue: run some (slow) validation checks on the graph and log any errors. Return false if there were errors.
		bool DebugCheckGraph(const FPBDConstraintGraph& ConstraintGraph) const;
#endif

		/**
		 * Get the level of a particle after particle levels are computed using ComputeContactGraphGBF 
		 */
		int32 GetParticleLevel(const FGeometryParticleHandle* ParticleHandle) const;

	private:
		void ComputeContactGraph(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId);
		void ComputeIslandColoring(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId);

		/**
		 * Compute contact graph using GBF paper Sec 8.1
		 */
		void ComputeContactGraphGBF(const FReal Dt, const int32 Island, const FPBDConstraintGraph& ConstraintGraph, const uint32 ContainerId);

		/**
		 * Compute the mapping between constraint graph node index to island graph node indices. 
		 * The mapping of dynamic particles are stored in TArray DynamicGlobalToIslandLocalNodeIndices. 
		 * The mapping of kinematic particles are stored in TMap KinematicGlobalToIslandLocalNodeIndices.
		 */
		void ComputeGlobalToIslandLocalNodeMapping(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices) const;

		/**
		 * Compute the graph node index in the island using the mapping data above.
		 */
		int32 GetIslandLocalNodeIdx(const int32 GlobalNodeIndex, const TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, const TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices, const bool IsNodeDynamic) const;

		struct FDirectedEdge
		{
			int32 FirstNode; // Or source node of the directed edge.
			int32 SecondNode; // Or target node of the directed edge. 
			FDirectedEdge(){};
			FDirectedEdge(const int32 FirstInput, const int32 SecondInput) : FirstNode(FirstInput), SecondNode(SecondInput) {};
		};

		/**
		 * Collect directed edges from the island.
		 * All collected edges will point from lower particles to upper particles.
		 * The set of collected directed edges is a subset of collision constraint edges.
		 * Horizontal edges will not be collected.
		 */
		void CollectIslandDirectedEdges(const FReal Dt, const int32 Island, const FPBDConstraintGraph& ConstraintGraph, const TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, const TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices, TArray<FDirectedEdge>& DirectedEdges) const;

		/**
		 * CSR: Compressed sparse row
		 * Build CSR representation of the graph from input edges
		 */

		void BuildGraphCSR(const TArray<FDirectedEdge>& GraphEdges, const int32 NumNodes, TArray<int32>& CSRIndices, TArray<int32>& CSRValues, TArray<int32>& CurrentIndices) const;

		/**
		 * A node is a root if it has no parents.
		 * Result is stored in IsRootNode
		 */
		void ComputeIsRootNode(const TArray<FDirectedEdge>& GraphEdges, const int32 NumNodes, TArray<bool> &IsRootNode) const;

		/**
		 * Digraph: directed graph (according to wikipedia: https://en.wikipedia.org/wiki/Directed_graph) that might contain loops
		 * DAG: directed acyclic graph
		 * Collapse directed graph to directed acyclic graph. Recursively collapse the loops in the directed graph to single nodes until there are no loops any more.
		 * For each digraph node, the corresponding DAG node index is stored in DigraphToDAGIndices
		 * For each DAG node, the corresponding list of digraph nodes is stored in DAGToDigraphIndices
		 */
		void CollapseDigraphToDAG(const TArray<int32>& ChildrenCSRIndices, const TArray<int32>& ChildrenCSRValues, const TArray<bool>& IsRootNode, TArray<int32>& DigraphToDAGIndices, TArray<TArray<int32>>& DAGToDigraphIndices, TArray<bool>& Visited, TArray<int32>& TraversalStack, TArray<int32>& NumOnStack) const;

		/**
		 * Compute DAG Edges from Digraph edges
		 */
		void ComputeDAGEdges(const TArray<FDirectedEdge>& DigraphEdges, const TArray<int32>& DigraphToDAGIndices, TArray<FDirectedEdge> &DAGEdges) const;

		/**
		 * Topological sort DAG nodes
		 * DAGToDigraphIndices is used to determine whether DAGI is a valid DAG node (point to non-zero Digraph nodes)
		 */
		void TopologicalSortDAG(const TArray<int32>& DAGChildrenCSRIndices, const TArray<int32>& DAGChildrenCSRValues, const TArray<TArray<int32>>& DAGToDigraphIndices, TArray<int32>& SortedDAGNodes, TArray<bool>& Visited) const;

		/**
		 * Assign levels to DAG nodes.
		 * We want all parents of DAGI have smaller levels than DAGI. The level of DAGI is the max distance to root nodes. 
		 */
		void AssignDAGNodeLevels(const TArray<int32>& DAGChildrenCSRIndices, const TArray<int32>& DAGChildrenCSRValues, const TArray<int32>& SortedDAGNodes, TArray<int32> &DAGNodeLevels) const;

		/**
		 * Assign edge levels based on DAG node levels.
		 * Edge level is the max of its end node levels.
		 */
		void AssignEdgeLevels(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, const TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, const TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices, const TArray<int32>& DigraphToDAGIndices, const TArray<int32> DAGNodeLevels);

		/**
		 * Update particle levels based on DAGNodeLevels.
		 */
		void UpdateParticleToLevel(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, const TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, const TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices, const TArray<int32>& DigraphToDAGIndices, const TArray<int32>& DAGNodeLevels);

		struct FGraphNodeColor
		{
			FGraphNodeColor()
				: NextColor(0)
			{
			}
			int32 NextColor;
			FColorSet UsedColors;
		};

		struct FGraphEdgeColor
		{
			FGraphEdgeColor()
				: Color(INDEX_NONE)
				, Level(INDEX_NONE)
			{
			}
			int32 Color;
			int32 Level;
		};

		struct FIslandColor
		{
			FIslandColor()
				: MaxColor(0)
				, MaxLevel(0)
				, NumEdges(0)
			{
			}
			int32 MaxColor;
			int32 MaxLevel;
			int32 NumEdges;
			FLevelToColorToConstraintListMap LevelToColorToConstraintListMap;
		};

		struct FGBFContactGraphData
		{
			TMap<int32, int32> KinematicGlobalToIslandLocalNodeIndices;
			TArray<FDirectedEdge> DigraphEdges;
			TArray<int32> ChildrenCSRIndices, ChildrenCSRValues;

			// Buffer array for BuildGraphCSR function. Not used outside BuildGraphCSR.
			TArray<int32> CSRCurrentIndices; 

			TArray<bool> IsRootNode;
			TArray<int32> DigraphToDAGIndices;
			TArray<TArray<int32>> DAGToDigraphIndices;

			// Helper arrays for CollapseDigraphToDAG and TopologicalSortDAG. Not used elsewhere
			TArray<bool> Visited; 
			TArray<int32> TraversalStack;
			TArray<int32> NumOnStack;

			TArray<FDirectedEdge> DAGEdges;
			TArray<int32> DAGChildrenCSRIndices, DAGChildrenCSRValues;
			TArray<int32> SortedDAGNodes;
			TArray<int32> DAGNodeLevels;
		};

		TArray<FGraphNodeColor> Nodes;
		TArray<FGraphEdgeColor> Edges;
		TArray<FIslandColor> IslandData;
		FLevelToColorToConstraintListMap EmptyLevelToColorToConstraintListMap;
		TArray<int32> DynamicGlobalToIslandLocalNodeIndicesArray; 
		TArray<int32> ParticleToLevel; 
		TArray<const TGeometryParticleHandle<FReal, 3>*> NodeToParticle;
		TArray<FGBFContactGraphData> IslandToGBFContactGraphData;
		bool bUseContactGraph;
	};

}
