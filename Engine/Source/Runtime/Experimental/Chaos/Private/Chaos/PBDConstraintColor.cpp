// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintColor.h"

#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"

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
DECLARE_CYCLE_STAT(TEXT("FPBDConstraintColor::ComputeIslandColoring"), STAT_Constraint_ComputeIslandColoring, STATGROUP_Chaos);

void FPBDConstraintColor::ComputeIslandColoring(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeIslandColoring);
	const TArray<TGeometryParticleHandle<FReal, 3>*>& IslandParticles = ConstraintGraph.GetIslandParticles(Island);
	FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = IslandData[Island].LevelToColorToConstraintListMap;
	int32& MaxColor = IslandData[Island].MaxColor;

	const int32 MaxLevel = IslandData[Island].MaxLevel;
	
	LevelToColorToConstraintListMap.Reset();
	LevelToColorToConstraintListMap.SetNum(MaxLevel + 1);
	MaxColor = -1;

	TSet<int32> ProcessedNodes;
	TArray<int32> NodesToProcess;

	for (const TGeometryParticleHandle<FReal, 3>* Particle :IslandParticles)
	{
		if (!ConstraintGraph.ParticleToNodeIndex.Find(Particle))
		{
			continue;
		}

		const int32 ParticleNodeIndex = ConstraintGraph.ParticleToNodeIndex[Particle];

		const bool bIsParticleDynamic = Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic;
		if (ProcessedNodes.Contains(ParticleNodeIndex) || bIsParticleDynamic == false)
		{
			continue;
		}

		NodesToProcess.Add(ParticleNodeIndex);

		while (NodesToProcess.Num())
		{
			const int32 NodeIndex = NodesToProcess.Last();
			const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.Nodes[NodeIndex];
			FGraphNodeColor& ColorNode = Nodes[NodeIndex];

			NodesToProcess.SetNum(NodesToProcess.Num() - 1, /*bAllowShrinking=*/false);
			ProcessedNodes.Add(NodeIndex);

			for (const int32 EdgeIndex : GraphNode.Edges)
			{
				const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.Edges[EdgeIndex];
				FGraphEdgeColor& ColorEdge = Edges[EdgeIndex];

				// If this is not from our rule, ignore it
				if (GraphEdge.Data.GetContainerId() != ContainerId)
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

					const typename FPBDConstraintGraph::FGraphNode& OtherGraphNode = ConstraintGraph.Nodes[OtherNodeIndex];
					const bool bIsOtherGraphNodeDynamic = OtherGraphNode.Particle->CastToRigidParticle() && OtherGraphNode.Particle->ObjectState() == EObjectStateType::Dynamic;
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

				LevelToColorToConstraintListMap[Level][ColorEdge.Color].Add(GraphEdge.Data.GetConstraintHandle());

				if (OtherNodeIndex != INDEX_NONE)
				{
					const typename FPBDConstraintGraph::FGraphNode& OtherGraphNode = ConstraintGraph.Nodes[OtherNodeIndex];
					const bool bIsOtherGraphNodeDynamic = OtherGraphNode.Particle->CastToRigidParticle() && OtherGraphNode.Particle->ObjectState() == EObjectStateType::Dynamic;
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
							ensure(OtherGraphNode.Island == GraphNode.Island);
							checkSlow(IslandParticles.Find(OtherGraphNode.Particle) != INDEX_NONE);
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
	const TArray<int32>& ConstraintDataIndices = ConstraintGraph.GetIslandConstraintData(Island);

	IslandData[Island].MaxLevel = ConstraintDataIndices.Num() ? 0 : -1;
	
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
		const int32* NodeIndexPtr = ConstraintGraph.ParticleToNodeIndex.Find(Particle);
		const bool bIsParticleDynamic = Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic;

		// We're only interested in static particles here to generate the graph (graph of dynamic objects touching static)
		if(bIsParticleDynamic == false && NodeIndexPtr)
		{
			const int32 NodeIndex = *NodeIndexPtr;

			const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.Nodes[NodeIndex];
			
			for(int32 EdgeIndex : GraphNode.Edges)
			{
				const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.Edges[EdgeIndex];

				// If this is not from our rule, ignore it
				if(GraphEdge.Data.GetContainerId() != ContainerId)
				{
					continue;
				}

				// Find adjacent node
				int32 OtherNode = INDEX_NONE;
				if(GraphEdge.FirstNode == NodeIndex)
				{
					OtherNode = GraphEdge.SecondNode;
				}
				else if(GraphEdge.SecondNode == NodeIndex)
				{
					OtherNode = GraphEdge.FirstNode;
				}

				// If we have a node, add it to the queue only if it matches our island. Statics have no island and can be touching dynamics of many islands
				// so we need to pick out only the edges that lead to the requested island to correctly build the graph. Implicitly all further edges must
				// be of the same island so we only need to do this check for level 1
				if(OtherNode != INDEX_NONE && ConstraintGraph.Nodes[OtherNode].Island == Island)
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

		int32 Level = Current.Level;
		int32 NodeIndex = Current.NodeIndex;
		const typename FPBDConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.Nodes[NodeIndex];

		for(int32 EdgeIndex : GraphNode.Edges)
		{
			const typename FPBDConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.Edges[EdgeIndex];
			FGraphEdgeColor& ColorEdge = Edges[EdgeIndex];

			// If this is not from our rule, ignore it
			if(GraphEdge.Data.GetContainerId() != ContainerId)
			{
				continue;
			}

			// If we have already been assigned a level, move on
			if(ColorEdge.Level >= 0)
			{
				continue;
			}

			// Find adjacent node and recurse
			int32 OtherNode = INDEX_NONE;
			if(GraphEdge.FirstNode == NodeIndex)
			{
				OtherNode = GraphEdge.SecondNode;
			}
			else if(GraphEdge.SecondNode == NodeIndex)
			{
				OtherNode = GraphEdge.FirstNode;
			}

			// Assign the level and update max level for the island if required
			// NOTE: if we hit a non-dynamic particle (node), it will contain all of
			// the contacts (edges) for dynamic particles interacting with it. They
			// may not all be in the same island, which is ok (e.g., two separated
			// boxes sat on a large plane). We need to ignore edges that are in other islands
			// @todo(chaos): we should probably store the island index with each edge.
			if (ConstraintGraph.Nodes[OtherNode].Island == Island)
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
	{
		for(const int32 EdgeIndex : ConstraintDataIndices)
		{
			check(Edges[EdgeIndex].Level <= IslandData[Island].MaxLevel);
			if(Edges[EdgeIndex].Level < 0)
			{
				Edges[EdgeIndex].Level = 0;
			}
		}
	}

	check(IslandData[Island].MaxLevel >= 0 || !ConstraintDataIndices.Num());
}

void FPBDConstraintColor::InitializeColor(const FPBDConstraintGraph& ConstraintGraph)
{
	// The Number of nodes is large and fairly constant so persist rather than resetting every frame
	if (Nodes.Num() != ConstraintGraph.Nodes.Num())
	{
		// Nodes need to grow when the nodes of the constraint graph grows
		Nodes.AddDefaulted(ConstraintGraph.Nodes.Num() - Nodes.Num());
	}
	
	// Reset the existing Nodes - so colors are all reset to zero
	for (int32 UpdatedNode : UpdatedNodes)
	{
		Nodes[UpdatedNode].NextColor = 0;
		Nodes[UpdatedNode].UsedColors.Empty();
	}

	// edges are not persistent right now so we still reset them
	Edges.Reset();
	IslandData.Reset();

	Edges.SetNum(ConstraintGraph.Edges.Num());
	IslandData.SetNum(ConstraintGraph.IslandToData.Num());

	UpdatedNodes = ConstraintGraph.GetUpdatedNodes();
}

void FPBDConstraintColor::ComputeColor(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	SCOPE_CYCLE_COUNTER(STAT_Constraint_ComputeColor);
	if (bUseContactGraph)
	{
		ComputeContactGraph(Island, ConstraintGraph, ContainerId);
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
