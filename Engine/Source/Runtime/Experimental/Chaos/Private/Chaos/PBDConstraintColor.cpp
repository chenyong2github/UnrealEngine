// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintColor.h"

#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDCollisionConstraints.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "ChaosStats.h"
#include "Containers/Queue.h"
#include "ChaosLog.h"

#include <memory>
#include <queue>
#include <sstream>
#include "Containers/RingBuffer.h"

using namespace Chaos;

DECLARE_CYCLE_STAT(TEXT("FPBDConstraintColor::ComputeColors"), STAT_Constraint_ComputeColor, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDConstraintColor::ComputeContactGraph"), STAT_Constraint_ComputeContactGraph, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDConstraintColor::ComputeContactGraphGBF"), STAT_Constraint_ComputeContactGraphGBF, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("FPBDConstraintColor::ComputeIslandColoring"), STAT_Constraint_ComputeIslandColoring, STATGROUP_Chaos);

bool bUseContactGraphGBF = false;
FAutoConsoleVariableRef CVarUseContactGraphGBF(TEXT("p.Chaos.UseContactGraphGBF"), bUseContactGraphGBF, TEXT(""));

void FPBDConstraintColor::ComputeIslandColoring(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeIslandColoring);

	// We need to sort the particles for the coloring to be deterministic
	// @todo(chaos): get rid of this sort and solve the problem at a higher level
	//const TArray<TGeometryParticleHandle<FReal, 3>*>& IslandParticles = ConstraintGraph.GetIslandParticles(Island);
	TArray<TGeometryParticleHandle<FReal, 3>*> IslandParticles = ConstraintGraph.GetIslandParticles(Island);
	IslandParticles.Sort([](const FGeometryParticleHandle& A, const FGeometryParticleHandle& B)
		{
			return A.ParticleID() < B.ParticleID();
		});

	FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = IslandData[Island].LevelToColorToConstraintListMap;
	int32& MaxColor = IslandData[Island].MaxColor;
	int32& MaxEdges = IslandData[Island].NumEdges;

	const int32 MaxLevel = IslandData[Island].MaxLevel;
	
	LevelToColorToConstraintListMap.Reset();
	LevelToColorToConstraintListMap.SetNum(MaxLevel + 1);
	MaxColor = -1;
	MaxEdges = 0;

	TSet<int32> ProcessedNodes;
	TArray<int32> NodesToProcess;

	for (const TGeometryParticleHandle<FReal, 3>* Particle :IslandParticles)
	{
		if (!ConstraintGraph.GetParticleNodes().Find(Particle))
		{
			continue;
		}

		const int32 ParticleNodeIndex = ConstraintGraph.GetParticleNodes()[Particle];

		const bool bIsParticleDynamic = Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic;
		if (ProcessedNodes.Contains(ParticleNodeIndex) || bIsParticleDynamic == false)
		{
			continue;
		}

		NodesToProcess.Add(ParticleNodeIndex);

		while (NodesToProcess.Num())
		{
			const int32 NodeIndex = NodesToProcess.Last();
			const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.GetGraphNodes()[NodeIndex];
			FGraphNodeColor& ColorNode = Nodes[NodeIndex];

			NodesToProcess.SetNum(NodesToProcess.Num() - 1, /*bAllowShrinking=*/false);
			ProcessedNodes.Add(NodeIndex); 


			for (const int32 EdgeIndex : GraphNode.NodeEdges)
			{
				const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.GetGraphEdges()[EdgeIndex];
				FGraphEdgeColor& ColorEdge = Edges[EdgeIndex];

				// If this is not from our rule, ignore it
				if (GraphEdge.ItemContainer != ContainerId)
				{
					continue;
				}

				// If edge has been colored skip it
				if (ColorEdge.Color >= 0)
				{
					continue;
				}

				// Get index to the other node on the edge
				int32 OtherNodeIndex = INDEX_NONE;
				if (GraphEdge.FirstNode == NodeIndex)
				{
					OtherNodeIndex = GraphEdge.SecondNode;
				}
				if (GraphEdge.SecondNode == NodeIndex)
				{
					OtherNodeIndex = GraphEdge.FirstNode;
				}

				// Find next color that is not used already at this node
				while (ColorNode.UsedColors.Contains(ColorNode.NextColor))
				{
					ColorNode.NextColor++;
				}
				int32 ColorToUse = ColorNode.NextColor;

				// Exclude colors used by the other node (but still allow this node to use them for other edges)
				if (OtherNodeIndex != INDEX_NONE)
				{
					FGraphNodeColor& OtherColorNode = Nodes[OtherNodeIndex];

					const typename FPBDConstraintGraph::FGraphNode& OtherGraphNode = ConstraintGraph.GetGraphNodes()[OtherNodeIndex];
					const bool bIsOtherGraphNodeDynamic = OtherGraphNode.NodeItem->CastToRigidParticle() && OtherGraphNode.NodeItem->ObjectState() == EObjectStateType::Dynamic;
					if (bIsOtherGraphNodeDynamic)
					{
						while (OtherColorNode.UsedColors.Contains(ColorToUse) || ColorNode.UsedColors.Contains(ColorToUse))
						{
							ColorToUse++;
						}
					}
				}

				// Assign color and set as used at this node
				MaxColor = FMath::Max(ColorToUse, MaxColor);
				ColorNode.UsedColors.Add(ColorToUse);
				ColorEdge.Color = ColorToUse;

				// Bump color to use next time, but only if we weren't forced to use a different color by the other node
				if ((ColorToUse == ColorNode.NextColor) && bIsParticleDynamic == true)
				{
					ColorNode.NextColor++;
				}

				int32 Level = ColorEdge.Level;

				if ((Level < 0) || (Level >= LevelToColorToConstraintListMap.Num()))
				{
					UE_LOG(LogChaos, Error, TEXT("\t **** Level is out of bounds!!!!  Level - %d, LevelToColorToConstraintListMap.Num() - %d"), Level, LevelToColorToConstraintListMap.Num());
					continue;
				}

				if (!LevelToColorToConstraintListMap[Level].Contains(ColorEdge.Color))
				{
					LevelToColorToConstraintListMap[Level].Add(ColorEdge.Color, {});
				}

				LevelToColorToConstraintListMap[Level][ColorEdge.Color].Add(GraphEdge.EdgeItem);
				++MaxEdges;

				if (OtherNodeIndex != INDEX_NONE)
				{
					const typename FPBDConstraintGraph::FGraphNode& OtherGraphNode = ConstraintGraph.GetGraphNodes()[OtherNodeIndex];
					const bool bIsOtherGraphNodeDynamic = OtherGraphNode.NodeItem->CastToRigidParticle() && OtherGraphNode.NodeItem->ObjectState() == EObjectStateType::Dynamic;
					if (bIsOtherGraphNodeDynamic)
					{
						FGraphNodeColor& OtherColorNode = Nodes[OtherNodeIndex];

						// Mark other node as not allowing use of this color
						if (bIsParticleDynamic)
						{
							OtherColorNode.UsedColors.Add(ColorEdge.Color);
						}

						// Queue other node for processing
						if (!ProcessedNodes.Contains(OtherNodeIndex))
						{
							ensure(OtherGraphNode.IslandIndex == GraphNode.IslandIndex);
							checkSlow(IslandParticles.Find(OtherGraphNode.NodeItem) != INDEX_NONE);
							NodesToProcess.Add(OtherNodeIndex);
						}
					}
				}
			}
		}
	}
}

void FPBDConstraintColor::ComputeContactGraph(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeContactGraph);
	const TArray<FConstraintHandleHolder>& IslandConstraints = ConstraintGraph.GetIslandConstraints(Island);

	IslandData[Island].MaxLevel = IslandConstraints.Num() ? 0 : -1;
	
	struct FLevelNodePair
	{
	FLevelNodePair()
		: Level(INDEX_NONE)
		, NodeIndex(INDEX_NONE)
	{}
	FLevelNodePair(int32 InLevel, int32 InNodeIndex)
		: Level(InLevel)
		, NodeIndex(InNodeIndex)
	{}

	int32 Level;
	int32 NodeIndex;
	};
	TRingBuffer<FLevelNodePair> NodeQueue(100);

	for(const TGeometryParticleHandle<FReal, 3> * Particle : ConstraintGraph.GetIslandParticles(Island))
	{
		const int32* NodeIndexPtr = ConstraintGraph.GetParticleNodes().Find(Particle);
		const bool bIsParticleDynamic = Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic;

		if (NodeIndexPtr)
		{
			// To be consistent with the solver body interface, we need to return a level given a particle. But internally we compute levels on nodes, therefore a mapping from nodes to particles is needed. 
			if (bIsParticleDynamic)
			{
				NodeToParticle[*NodeIndexPtr] = Particle;
			}
		}
		// We're only interested in static particles here to generate the graph (graph of dynamic objects touching static)
		if(bIsParticleDynamic == false && NodeIndexPtr)
		{
			const int32 NodeIndex = *NodeIndexPtr;
			const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.GetGraphNodes()[NodeIndex];
			

			for (int32 EdgeIndex : GraphNode.NodeEdges)
			{
				const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.GetGraphEdges()[EdgeIndex];

				// If this is not from our rule, ignore it
				if (GraphEdge.ItemContainer != ContainerId)
				{
					continue;
				}

				// Find adjacent node
				int32 OtherNode = INDEX_NONE;
				if (GraphEdge.FirstNode == NodeIndex)
				{
					OtherNode = GraphEdge.SecondNode;
				}
				else if (GraphEdge.SecondNode == NodeIndex)
				{
					OtherNode = GraphEdge.FirstNode;
				}

				// If we have a node, add it to the queue only if it matches our island. Statics have no island and can be touching dynamics of many islands
				// so we need to pick out only the edges that lead to the requested island to correctly build the graph. Implicitly all further edges must
				// be of the same island so we only need to do this check for level 1
				if (OtherNode != INDEX_NONE && ConstraintGraph.GetGraphNodes()[OtherNode].IslandIndex == ConstraintGraph.GetGraphIndex(Island))
				{
					Edges[EdgeIndex].Level = 0;
					NodeQueue.Emplace(1, OtherNode);
				}
			}
		}
	}

	FLevelNodePair Current;
	while(!NodeQueue.IsEmpty())
	{
		Current = NodeQueue.First();
		NodeQueue.PopFrontNoCheck();

		const int32 Level = Current.Level;
		const int32 NodeIndex = Current.NodeIndex;
		const TGeometryParticleHandle<FReal, 3> *Particle = NodeToParticle[NodeIndex];
		if (Particle)
		{
			ParticleToLevel[Particle->UniqueIdx().Idx] = Level;
		} 
		const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.GetGraphNodes()[NodeIndex];
		
		for (int32 EdgeIndex : GraphNode.NodeEdges)
		{
			const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.GetGraphEdges()[EdgeIndex];
			FGraphEdgeColor& ColorEdge = Edges[EdgeIndex];

			// If this is not from our rule, ignore it
			if (GraphEdge.ItemContainer != ContainerId)
			{
				continue;
			}

			// If we have already been assigned a level, move on
			if (ColorEdge.Level >= 0)
			{
				continue;
			}

			// Find adjacent node and recurse
			int32 OtherNode = INDEX_NONE;
			if (GraphEdge.FirstNode == NodeIndex)
			{
				OtherNode = GraphEdge.SecondNode;
			}
			else if (GraphEdge.SecondNode == NodeIndex)
			{
				OtherNode = GraphEdge.FirstNode;
			}

			// Assign the level and update max level for the island if required
			// NOTE: if we hit a non-dynamic particle (node), it will contain all of
			// the contacts (edges) for dynamic particles interacting with it. They
			// may not all be in the same island, which is ok (e.g., two separated
			// boxes sat on a large plane). We need to ignore edges that are in other islands
			// @todo(chaos): we should probably store the island index with each edge.
			if (ConstraintGraph.GetGraphNodes()[OtherNode].IslandIndex == ConstraintGraph.GetGraphIndex(Island))
			{
				ColorEdge.Level = Level;
				IslandData[Island].MaxLevel = FGenericPlatformMath::Max(IslandData[Island].MaxLevel, ColorEdge.Level);

				// If we have an other node, append it to our queue on the next level
				if (OtherNode != INDEX_NONE)
				{
					NodeQueue.Emplace(ColorEdge.Level + 1, OtherNode);
				}
			}
		}
	}

	// An isolated island that is only dynamics will not have been processed above, put everything without a level into level zero
	// #BGTODO this can surely be done as we build the edges, after this function everything will be at least level 0 so we can probably construct them
	//         in that level to avoid a potentially large iteration here.
	for(const FConstraintHandle* IslandConstraint : IslandConstraints)
	{
		if(IslandConstraint)
		{
			const int32 EdgeIndex = IslandConstraint->ConstraintGraphIndex();
			if(Edges.IsValidIndex(EdgeIndex))
			{
				check(Edges[EdgeIndex].Level <= IslandData[Island].MaxLevel);
				if(Edges[EdgeIndex].Level < 0)
				{
					Edges[EdgeIndex].Level = 0;
				}
			}
		} 
	}

	check(IslandData[Island].MaxLevel >= 0 || !IslandConstraints.Num());
}

void FPBDConstraintColor::ComputeContactGraphGBF(const FReal Dt, const int32 Island, const FPBDConstraintGraph& ConstraintGraph, const uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeContactGraphGBF);
	FGBFContactGraphData& Data = IslandToGBFContactGraphData[Island];
	// First create an island-local graph. The mapping is stored in DynamicGlobalToIslandLocalNodeIndicesArray and KinematicGlobalToIslandLocalNodeIndices.
	ComputeGlobalToIslandLocalNodeMapping(Island, ConstraintGraph, DynamicGlobalToIslandLocalNodeIndicesArray, Data.KinematicGlobalToIslandLocalNodeIndices);
	CollectIslandDirectedEdges(Dt, Island, ConstraintGraph, DynamicGlobalToIslandLocalNodeIndicesArray, Data.KinematicGlobalToIslandLocalNodeIndices, Data.DigraphEdges);

	// Build compressed sparse row (CSR) representation of the directed graph (digraph).
	const int32 NumNodes=ConstraintGraph.GetIslandParticles(Island).Num();
	BuildGraphCSR(Data.DigraphEdges, NumNodes, Data.ChildrenCSRIndices, Data.ChildrenCSRValues, Data.CSRCurrentIndices);

	// Root nodes: nodes have no parents.
	ComputeIsRootNode(Data.DigraphEdges, NumNodes, Data.IsRootNode);

	// Collapse directed graph (digraph) to direct acyclic graph (DAG). This is a many-to-one mapping.
	CollapseDigraphToDAG(Data.ChildrenCSRIndices, Data.ChildrenCSRValues, Data.IsRootNode, Data.DigraphToDAGIndices, Data.DAGToDigraphIndices, Data.Visited, Data.TraversalStack, Data.NumOnStack);

	// Compute the DAG edges from the digraph edges 
	ComputeDAGEdges(Data.DigraphEdges, Data.DigraphToDAGIndices, Data.DAGEdges);
	BuildGraphCSR(Data.DAGEdges, NumNodes, Data.DAGChildrenCSRIndices, Data.DAGChildrenCSRValues, Data.CSRCurrentIndices);

	// Topological sort on DAG nodes
	TopologicalSortDAG(Data.DAGChildrenCSRIndices, Data.DAGChildrenCSRValues, Data.DAGToDigraphIndices, Data.SortedDAGNodes, Data.Visited);

	AssignDAGNodeLevels(Data.DAGChildrenCSRIndices, Data.DAGChildrenCSRValues, Data.SortedDAGNodes, Data.DAGNodeLevels);
	AssignEdgeLevels(Island, ConstraintGraph, DynamicGlobalToIslandLocalNodeIndicesArray, Data.KinematicGlobalToIslandLocalNodeIndices, Data.DigraphToDAGIndices, Data.DAGNodeLevels);
	UpdateParticleToLevel(Island, ConstraintGraph, DynamicGlobalToIslandLocalNodeIndicesArray, Data.KinematicGlobalToIslandLocalNodeIndices, Data.DigraphToDAGIndices, Data.DAGNodeLevels);
}

void FPBDConstraintColor::ComputeGlobalToIslandLocalNodeMapping(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices) const
{
	const TArray<FGeometryParticleHandle*>& IslandParticles = ConstraintGraph.GetIslandParticles(Island);
	const int32 NumIslandParticles = IslandParticles.Num();
	KinematicGlobalToIslandLocalNodeIndices.Reset();
	for(int32 GraphNodeI = 0; GraphNodeI < NumIslandParticles; GraphNodeI++)
	{
		const TGeometryParticleHandle<FReal, 3>* Particle = IslandParticles[GraphNodeI];
		const int32* NodeIndexPtr = ConstraintGraph.GetParticleNodes().Find(Particle);
		if (NodeIndexPtr)
		{
			const int32 NodeIndex = *NodeIndexPtr;
			const bool IsDynamic = ConstraintGraph.GetGraphNodes()[NodeIndex].IslandIndex != INDEX_NONE;
			if (IsDynamic)
			{
				DynamicGlobalToIslandLocalNodeIndices[NodeIndex] = GraphNodeI;
			}
			else
			{
				KinematicGlobalToIslandLocalNodeIndices.FindOrAdd(NodeIndex) = GraphNodeI;
			}
		}
	}
}

int32 FPBDConstraintColor::GetIslandLocalNodeIdx(const int32 GlobalNodeIndex, const TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, const TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices, const bool IsNodeDynamic) const
{
	if (IsNodeDynamic)
	{
		return DynamicGlobalToIslandLocalNodeIndices[GlobalNodeIndex];
	}
	else
	{
		return KinematicGlobalToIslandLocalNodeIndices[GlobalNodeIndex];
	}
}

void FPBDConstraintColor::CollectIslandDirectedEdges(const FReal Dt, const int32 Island, const FPBDConstraintGraph& ConstraintGraph, const TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, const TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices, TArray<FDirectedEdge>& DirectedEdges) const
{
	DirectedEdges.Reset();
	const int32 NumIslandParticles = ConstraintGraph.GetIslandParticles(Island).Num();

	for (const FConstraintHandle* IslandConstraint : ConstraintGraph.GetIslandConstraints(Island))
	{
		if(IslandConstraint && ConstraintGraph.GetGraphEdges().IsValidIndex(IslandConstraint->ConstraintGraphIndex()))
		{
			const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.GetGraphEdges()[IslandConstraint->ConstraintGraphIndex()];

			const FPBDCollisionConstraintHandle *CollisionHandle = GraphEdge.EdgeItem->As<FPBDCollisionConstraintHandle>();
			if (CollisionHandle)
			{
				const bool IsFirstNodeDynamic = ConstraintGraph.GetGraphNodes()[GraphEdge.FirstNode].IslandIndex != INDEX_NONE;
				const int32 FirstLocalNodeIndex = GetIslandLocalNodeIdx(GraphEdge.FirstNode, DynamicGlobalToIslandLocalNodeIndices, KinematicGlobalToIslandLocalNodeIndices, IsFirstNodeDynamic);
				const bool IsSecondNodeDynamic = ConstraintGraph.GetGraphNodes()[GraphEdge.SecondNode].IslandIndex != INDEX_NONE;
				const int32 SecondLocalNodeIndex = GetIslandLocalNodeIdx(GraphEdge.SecondNode, DynamicGlobalToIslandLocalNodeIndices, KinematicGlobalToIslandLocalNodeIndices, IsSecondNodeDynamic);

				const ECollisionConstraintDirection ConstraintDirection = CollisionHandle->GetContact().GetConstraintDirection(Dt);
				if(ConstraintDirection == ECollisionConstraintDirection::Particle0ToParticle1)
				{
					DirectedEdges.Push(FDirectedEdge(FirstLocalNodeIndex, SecondLocalNodeIndex));
				}
				else if(ConstraintDirection == ECollisionConstraintDirection::Particle1ToParticle0)
				{
					DirectedEdges.Push(FDirectedEdge(SecondLocalNodeIndex, FirstLocalNodeIndex));
				}
			}
		}
	}
}

/**
 * The children of node i is stored from CSRValues[CSRIndices[i]] to CSRValues[CSRIndices[i+1]]
 */
void FPBDConstraintColor::BuildGraphCSR(const TArray<FDirectedEdge>& GraphEdges, const int32 NumNodes, TArray<int32>& CSRIndices, TArray<int32>& CSRValues, TArray<int32>& CurrentIndices) const
{
	CSRIndices.Init(0, NumNodes + 1);
	if (NumNodes == 0)
	{
		return;
	}
	const int32 NumEdges = GraphEdges.Num();
	CSRValues.Init(-1, NumEdges);
	// Collect the number of children of each node and store them in CSRIndices[i+1]
	for(const FDirectedEdge& Edge : GraphEdges)
	{
		CSRIndices[Edge.FirstNode + 1] += 1;
	}
	// CurrentIndices[i] is the first available vacancy for node i to populate its children
	CurrentIndices.Init(0, NumNodes);
	CurrentIndices[0] = 0;
	if (NumNodes > 1)
	{
		CurrentIndices[1] = CSRIndices[1];
		for(int32 i = 2; i < NumNodes; i++)
		{
			CSRIndices[i] += CSRIndices[i-1];
			CurrentIndices[i] = CSRIndices[i];
		}
	}

	// Populate edges
	CSRIndices[NumNodes] = NumEdges;
	for(const FDirectedEdge& Edge : GraphEdges)
	{
		CSRValues[CurrentIndices[Edge.FirstNode]] = Edge.SecondNode;
		CurrentIndices[Edge.FirstNode]++;
	}
}

void FPBDConstraintColor::ComputeIsRootNode(const TArray<FDirectedEdge>& GraphEdges, const int32 NumNodes, TArray<bool> &IsRootNode) const
{
	IsRootNode.SetNum(NumNodes);
	for (bool& B : IsRootNode)
	{
		B = true;
	}
	for(const FDirectedEdge& Edge : GraphEdges)
	{
		if (IsRootNode[Edge.SecondNode])
		{
			IsRootNode[Edge.SecondNode] = false;
		}
	}
}

void FPBDConstraintColor::CollapseDigraphToDAG(const TArray<int32>& ChildrenCSRIndices, const TArray<int32>& ChildrenCSRValues, const TArray<bool>& IsRootNode, TArray<int32>& DigraphToDAGIndices, TArray<TArray<int32>>& DAGToDigraphIndices, TArray<bool>& Visited, TArray<int32>& TraversalStack, TArray<int32>& NumOnStack) const
{
	const int32 NumDigraphNodes = ChildrenCSRIndices.Num() - 1;
	DigraphToDAGIndices.SetNum(NumDigraphNodes);
	DAGToDigraphIndices.SetNum(NumDigraphNodes);

	// Initialize identity mapping between Digraph and DAG
	for(int32 i = 0; i < NumDigraphNodes; i++)
	{
		DigraphToDAGIndices[i] = i;
		DAGToDigraphIndices[i].SetNum(1);
		DAGToDigraphIndices[i][0] = i; 
	}

	// If Visited[**DigraphI**] == true, the node DigraphI and its children are processed and will not be traversed in the future.
	Visited.Init(false, NumDigraphNodes);

	// Every **Digraph** node will be pushed to TraversalStack before they are processed and will be popped from the stack after the process finished.
	TraversalStack.Reserve(64);
	TraversalStack.Reset();

	// NumOnStack[**DAGI**] is the number of Digraph nodes that are collapsed to DAGI and are currently on TraversalStack.
	NumOnStack.Init(0, NumDigraphNodes);


	TFunctionRef<void(const int32)> CollapseLoop = [&DigraphToDAGIndices, &DAGToDigraphIndices, &TraversalStack, &NumOnStack](const int32 DigraphI)
	{
		const int32 DAGI = DigraphToDAGIndices[DigraphI];
		// Go through the traversal stack in reverse order until we find another node that is pointed to DAGI. Collapse all the nodes traversed this way to DAGI.
		// We collapse these nodes to DAGI, but we don't remove them from the traversal stack. We will still need to visit their children later.
		for(int32 j = TraversalStack.Num() - 1; DigraphToDAGIndices[TraversalStack[j]] != DAGI; j--)
		{
			const int32 DigraphJ = TraversalStack[j];
			const int32 DAGJ = DigraphToDAGIndices[DigraphJ];
			// Merge DAGI and DAGJ. Move all the nodes in DAGToDigraphIndices[DAGJ] to DAGToDigraphIndices[DAGI].
			const int32 Num = DAGToDigraphIndices[DAGJ].Num();
			if (Num > 0)
			{
				for(int32 k = 0; k < Num; k++)
				{
					const int32 DigraphK = DAGToDigraphIndices[DAGJ][k];
					DigraphToDAGIndices[DigraphK] = DAGI;
					DAGToDigraphIndices[DAGI].Push(DigraphK);
				}
				DAGToDigraphIndices[DAGJ].Empty();
				NumOnStack[DAGJ] -= Num;
				NumOnStack[DAGI] += Num;
			}
		}
	};

	// For simplicity, traverse on **Digraph** nodes so we don't need to change graph edges as we collapsed digraph to DAG.
	TFunctionRef<void(const int32)> TraverseAndCollapse = [&DigraphToDAGIndices, &NumOnStack, &CollapseLoop, &TraversalStack, &Visited, &ChildrenCSRIndices, &ChildrenCSRValues, &TraverseAndCollapse](const int32 DigraphI)
	{
		int32 DAGI = DigraphToDAGIndices[DigraphI];
		// If there are already Digraph nodes that point to DAGI on the stack, collapse the loop.
		if (NumOnStack[DAGI] > 0)
		{
			CollapseLoop(DigraphI);
		}
		else
		{
			const int32 NumEdges = ChildrenCSRIndices[DigraphI + 1] - ChildrenCSRIndices[DigraphI];
			// For non-leaf nodes
			if (NumEdges > 0)
			{
				TraversalStack.Push(DigraphI);
				NumOnStack[DAGI]++;
				for(int32 j = ChildrenCSRIndices[DigraphI]; j < ChildrenCSRIndices[DigraphI + 1]; j++)
				{
					const int32 DigraphJ = ChildrenCSRValues[j];
					if (Visited[DigraphJ])
					{
						continue;
					}
					TraverseAndCollapse(DigraphJ);
				}
				// Since there might have been collapsing of the loops and thereby the mapped DAG node might be modified, DAGI needs to be updated here.
				DAGI = DigraphToDAGIndices[DigraphI];
				// Popping from the stack
				NumOnStack[DAGI]--;
				TraversalStack.Pop(false/*bAllowShrinking*/); 
			}
			Visited[DigraphI]=true;
		}
	};

	// First traverse all the root nodes that have no parents.
	for (int32 DigraphI = 0; DigraphI < NumDigraphNodes; DigraphI++)
	{
		if (IsRootNode[DigraphI])
		{
			TraverseAndCollapse(DigraphI);
		}
	}

	// Next traverse the rest loops.
	for(int32 DigraphI = 0; DigraphI < NumDigraphNodes; DigraphI++)
	{
		if(!Visited[DigraphI])
		{
			TraverseAndCollapse(DigraphI);
		}
	}
}

void FPBDConstraintColor::ComputeDAGEdges(const TArray<FDirectedEdge>& DigraphEdges, const TArray<int32>& DigraphToDAGIndices, TArray<FDirectedEdge> &DAGEdges) const
{
	DAGEdges.Reset(DigraphEdges.Num());
	for (const FDirectedEdge &Edge : DigraphEdges)
	{
		const int32 DAGFirst = DigraphToDAGIndices[Edge.FirstNode];
		const int32 DAGSecond = DigraphToDAGIndices[Edge.SecondNode];
		// Remove self loops
		if (DAGFirst != DAGSecond)
		{
			DAGEdges.Push({DAGFirst, DAGSecond});
		}
	}
}

void FPBDConstraintColor::TopologicalSortDAG(const TArray<int32>& DAGChildrenCSRIndices, const TArray<int32>& DAGChildrenCSRValues, const TArray<TArray<int32>>& DAGToDigraphIndices, TArray<int32>& SortedDAGNodes, TArray<bool>& Visited) const
{
	const int32 NumNodes = DAGToDigraphIndices.Num();
	SortedDAGNodes.Reset(NumNodes);
	Visited.Init(false, NumNodes);
	TFunctionRef<void(const int32)> Traverse = [&DAGChildrenCSRIndices, &DAGChildrenCSRValues, &SortedDAGNodes, &Visited, &Traverse](const int32 DAGNodeI)
	{
		Visited[DAGNodeI] = true;
		for (int32 i = DAGChildrenCSRIndices[DAGNodeI]; i < DAGChildrenCSRIndices[DAGNodeI + 1]; i++)
		{
			const int32 Child = DAGChildrenCSRValues[i];
			if (!Visited[Child])
			{
				Traverse(Child);
			}
		}
		// Push DAGNodeI after all its children are pushed.
		SortedDAGNodes.Push(DAGNodeI);
	};
	for(int32 i = 0; i < NumNodes; i++)
	{
		const bool IsValidDAGNode = DAGToDigraphIndices[i].Num() > 0; 
		if (!IsValidDAGNode)
		{
			continue;
		}
		if (Visited[i])
		{
			continue;
		}
		Traverse(i);
	}
}

void FPBDConstraintColor::AssignDAGNodeLevels(const TArray<int32>& DAGChildrenCSRIndices, const TArray<int32>& DAGChildrenCSRValues, const TArray<int32>& SortedDAGNodes, TArray<int32> &DAGNodeLevels) const
{
	const int32 NumNodes = DAGChildrenCSRIndices.Num() - 1;
	DAGNodeLevels.Init(0, NumNodes);
	for (int32 i = SortedDAGNodes.Num() - 1; i >= 0; i--)
	{
		const int32 DAGNodeI = SortedDAGNodes[i];
		for (int32 j = DAGChildrenCSRIndices[DAGNodeI]; j < DAGChildrenCSRIndices[DAGNodeI + 1]; j++)
		{
			const int32 Child = DAGChildrenCSRValues[j];
			// Level of DAGI is the max distance to root nodes
			DAGNodeLevels[Child] = FMath::Max(DAGNodeLevels[Child], DAGNodeLevels[DAGNodeI] + 1);
		}
	}
}

void FPBDConstraintColor::AssignEdgeLevels(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, const TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, const TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices, const TArray<int32>& DigraphToDAGIndices, const TArray<int32> DAGNodeLevels)
{
	for(const FConstraintHandle* IslandConstraint : ConstraintGraph.GetIslandConstraints(Island))
	{
		if(IslandConstraint && ConstraintGraph.GetGraphEdges().IsValidIndex(IslandConstraint->ConstraintGraphIndex()))
		{
			const int32 EdgeIndex = IslandConstraint->ConstraintGraphIndex();
			const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.GetGraphEdges()[EdgeIndex];

			const bool IsFirstNodeDynamic = ConstraintGraph.GetGraphNodes()[GraphEdge.FirstNode].IslandIndex != INDEX_NONE;
			const int32 FirstLocalNodeIndex = GetIslandLocalNodeIdx(GraphEdge.FirstNode, DynamicGlobalToIslandLocalNodeIndices, KinematicGlobalToIslandLocalNodeIndices, IsFirstNodeDynamic);
			const int32 FirstLevel = DAGNodeLevels[DigraphToDAGIndices[FirstLocalNodeIndex]];
			const bool IsSecondNodeDynamic = ConstraintGraph.GetGraphNodes()[GraphEdge.SecondNode].IslandIndex != INDEX_NONE;
			const int32 SecondLocalNodeIndex = GetIslandLocalNodeIdx(GraphEdge.SecondNode, DynamicGlobalToIslandLocalNodeIndices, KinematicGlobalToIslandLocalNodeIndices, IsSecondNodeDynamic);
			const int32 SecondLevel = DAGNodeLevels[DigraphToDAGIndices[SecondLocalNodeIndex]];

			// Edge level is the max of node level
			const int32 EdgeLevel = FMath::Max(FirstLevel, SecondLevel);
			Edges[EdgeIndex].Level = EdgeLevel;
			IslandData[Island].MaxLevel = FMath::Max(IslandData[Island].MaxLevel, EdgeLevel);
		}
	}
}

void FPBDConstraintColor::UpdateParticleToLevel(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, const TArray<int32>& DynamicGlobalToIslandLocalNodeIndices, const TMap<int32, int32>& KinematicGlobalToIslandLocalNodeIndices, const TArray<int32>& DigraphToDAGIndices, const TArray<int32>& DAGNodeLevels)
{
	for(const TGeometryParticleHandle<FReal, 3> * Particle : ConstraintGraph.GetIslandParticles(Island))
	{
		const int32* NodeIndexPtr = ConstraintGraph.GetParticleNodes().Find(Particle);
		if (NodeIndexPtr)
		{
			const int32 NodeIndex = *NodeIndexPtr;
			const int32 ParticleIdx = Particle->UniqueIdx().Idx;
			const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.GetGraphNodes()[NodeIndex];
			const bool IsNodeDynamic = GraphNode.IslandIndex != INDEX_NONE;
			if (IsNodeDynamic)
			{
				const int32 IslandDigraphI = GetIslandLocalNodeIdx(NodeIndex, DynamicGlobalToIslandLocalNodeIndices, KinematicGlobalToIslandLocalNodeIndices, IsNodeDynamic);
				const int32 Level = DAGNodeLevels[DigraphToDAGIndices[IslandDigraphI]];
				ParticleToLevel[ParticleIdx] = Level;	
			}
			else
			{
				// Set kinematic particle levels to 0
				ParticleToLevel[ParticleIdx] = 0;
			}
		}
	}
}

int32 FPBDConstraintColor::GetParticleLevel(const FGeometryParticleHandle* ParticleHandle) const
{
	// todo(chaos) the index check should nopt be necessarybut right ParticleToLevel is not as larghe as the largest uniqueIdx in the graph( because of MaxParticleIndex not reflecting this )
	const int32 UniqueIdx = ParticleHandle->UniqueIdx().Idx;
	if (ParticleToLevel.IsValidIndex(UniqueIdx))
	{
		return ParticleToLevel[ParticleHandle->UniqueIdx().Idx];
	}
	return 0;
}

void FPBDConstraintColor::InitializeColor(const FPBDConstraintGraph& ConstraintGraph)
{
	// The Number of nodes is large and fairly constant so persist rather than resetting every frame
	if (Nodes.Num() != ConstraintGraph.GetGraphNodes().GetMaxIndex())
	{
		// Nodes need to grow when the nodes of the constraint graph grows
		Nodes.AddDefaulted(ConstraintGraph.GetGraphNodes().GetMaxIndex() - Nodes.Num());
	}

	for (auto& Node : Nodes)
	{
		Node.NextColor = 0;
		Node.UsedColors.Empty();
	}

	// edges are not persistent right now so we still reset them
	Edges.Reset();
	IslandData.Reset();

	Edges.SetNum(ConstraintGraph.GetGraphEdges().GetMaxIndex());
	IslandData.SetNum(ConstraintGraph.NumIslands());
	ParticleToLevel.Init(0, ConstraintGraph.GetMaxParticleUniqueIdx() + 1);
	DynamicGlobalToIslandLocalNodeIndicesArray.Init(-1, ConstraintGraph.GetIslandGraph()->MaxNumNodes());
	NodeToParticle.Init(nullptr, ConstraintGraph.GetIslandGraph()->MaxNumNodes());
	IslandToGBFContactGraphData.SetNum(ConstraintGraph.NumIslands());
}

void FPBDConstraintColor::ComputeColor(const FReal Dt, const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeColor);
	if (bUseContactGraph)
	{
		if (bUseContactGraphGBF)
		{
			// Hack: extra cautious to make sure ComputeContactGraphGBF is only called once per frame.
			// Ideally this would be ContainerType == CollisionConstraints
			if (ContainerId == 0) 
			{
				ComputeContactGraphGBF(Dt, Island, ConstraintGraph, ContainerId);
			}
		}
		else
		{
			ComputeContactGraph(Island, ConstraintGraph, ContainerId);
		}
	}
	else
	{
		for (FGraphEdgeColor& Edge : Edges)
		{
			Edge.Level = 0;
		}
		IslandData[Island].MaxLevel = 0;
	}
	ComputeIslandColoring(Island, ConstraintGraph, ContainerId);
}

const typename FPBDConstraintColor::FLevelToColorToConstraintListMap& FPBDConstraintColor::GetIslandLevelToColorToConstraintListMap(int32 Island) const
{
	if (Island < IslandData.Num())
	{
		return IslandData[Island].LevelToColorToConstraintListMap;
	}
	return EmptyLevelToColorToConstraintListMap;
}

int FPBDConstraintColor::GetIslandMaxColor(int32 Island) const
{
	if (Island < IslandData.Num())
	{
		return IslandData[Island].MaxColor;
	}
	return -1;
}

int FPBDConstraintColor::GetIslandMaxLevel(int32 Island) const
{
	if (Island < IslandData.Num())
	{
		return IslandData[Island].MaxLevel;
	}
	return -1;
}
