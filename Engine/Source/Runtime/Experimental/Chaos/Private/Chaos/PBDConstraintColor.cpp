// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintColor.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "ChaosStats.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/PBDRigidParticles.h"
#include "Containers/Queue.h"

#include <memory>
#include <queue>
#include <sstream>

using namespace Chaos;

template<typename T, int d>
void TPBDConstraintColor<T, d>::ComputeIslandColoring(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const int32 Island, const FConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	const TArray<int32>& IslandParticleIndices = ConstraintGraph.GetIslandParticles(Island);
	FLevelToColorToConstraintListMap& LevelToColorToConstraintListMap = IslandData[Island].LevelToColorToConstraintListMap;
	int32& MaxColor = IslandData[Island].MaxColor;

#ifdef USE_CONTACT_LEVELS
	const int32 MaxLevel = IslandData[Island].MaxLevel;
#else
	const int32 MaxLevel = 0;
#endif
	
	LevelToColorToConstraintListMap.Reset();
	LevelToColorToConstraintListMap.SetNum(MaxLevel + 1);
	MaxColor = -1;

	TSet<int32> ProcessedNodes;
	TArray<int32> NodesToProcess;

	for (const int32 ParticleIndex : IslandParticleIndices)
	{
		if (!ConstraintGraph.ParticleToNodeIndex.Find(ParticleIndex))
		{
			continue;
		}

		const int32 ParticleNodeIndex = ConstraintGraph.ParticleToNodeIndex[ParticleIndex];

		if (ProcessedNodes.Contains(ParticleNodeIndex) || !InParticles.InvM(ConstraintGraph.Nodes[ParticleNodeIndex].BodyIndex))
		{
			continue;
		}

		NodesToProcess.Add(ParticleNodeIndex);

		while (NodesToProcess.Num())
		{
			const int32 NodeIndex = NodesToProcess.Last();
			const typename FConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.Nodes[NodeIndex];
			FGraphNodeColor& ColorNode = Nodes[NodeIndex];

			NodesToProcess.SetNum(NodesToProcess.Num() - 1, /*bAllowShrinking=*/false);
			ProcessedNodes.Add(NodeIndex);

			for (const int32 EdgeIndex : GraphNode.Edges)
			{
				const typename FConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.Edges[EdgeIndex];
				FGraphEdgeColor& ColorEdge = Edges[EdgeIndex];

				// If this is not from our rule, ignore it
				if (GraphEdge.Data.ContainerId != ContainerId)
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
					while (OtherColorNode.UsedColors.Contains(ColorToUse) || ColorNode.UsedColors.Contains(ColorToUse))
					{
						ColorToUse++;
					}
				}

				// Assign color and set as used at this node
				MaxColor = FMath::Max(ColorToUse, MaxColor);
				ColorNode.UsedColors.Add(ColorToUse);
				ColorEdge.Color = ColorToUse;

				// Bump color to use next time, but only if we weren't forced to use a different color by the other node
				if ((ColorToUse == ColorNode.NextColor) && InParticles.InvM(GraphNode.BodyIndex))
				{
					ColorNode.NextColor++;
				}

#ifdef USE_CONTACT_LEVELS
				int32 Level = ColorEdge.Level;
#else
				int32 Level = 0;
#endif

				if ((Level < 0) || (Level >= LevelToColorToConstraintListMap.Num()))
				{
					UE_LOG(LogChaos, Error, TEXT("\t **** Level is out of bounds!!!!  Level - %d, LevelToColorToConstraintListMap.Num() - %d"), Level, LevelToColorToConstraintListMap.Num());
					continue;
				}

				if (!LevelToColorToConstraintListMap[Level].Contains(ColorEdge.Color))
				{
					LevelToColorToConstraintListMap[Level].Add(ColorEdge.Color, {});
				}

				LevelToColorToConstraintListMap[Level][ColorEdge.Color].Add(GraphEdge.Data.ConstraintIndex);

				if (OtherNodeIndex != INDEX_NONE)
				{
					const typename FConstraintGraph::FGraphNode& OtherGraphNode = ConstraintGraph.Nodes[OtherNodeIndex];
					FGraphNodeColor& OtherColorNode = Nodes[OtherNodeIndex];

					// Mark other node as not allowing use of this color
					if (InParticles.InvM(OtherGraphNode.BodyIndex))
					{
						OtherColorNode.UsedColors.Add(ColorEdge.Color);
					}

					// Queue other node for processing
					if (!ProcessedNodes.Contains(OtherNodeIndex) && InParticles.InvM(OtherGraphNode.BodyIndex))
					{
						ensure(OtherGraphNode.Island == GraphNode.Island);
						checkSlow(IslandParticleIndices.Find(OtherGraphNode.BodyIndex) != INDEX_NONE);
						NodesToProcess.Add(OtherNodeIndex);
					}
				}
			}
		}
	}
}

#ifdef USE_CONTACT_LEVELS
template<typename T, int d>
void TPBDConstraintColor<T, d>::ComputeContactGraph(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const int32 Island, const FConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
	const TArray<int32>& ConstraintDataIndices = ConstraintGraph.GetIslandConstraintData(Island);

	IslandData[Island].MaxLevel = ConstraintDataIndices.Num() ? 0 : -1;

	std::queue<std::pair<int32, int32>> QueueToProcess;
	for (const int32 Index : InIndices)
	{
		const int32* NodeIndexPtr = ConstraintGraph.ParticleToNodeIndex.Find(Index);
		if (!InParticles.InvM(Index) && NodeIndexPtr)
		{
			const int32 NodeIndex = *NodeIndexPtr;
			QueueToProcess.push(std::make_pair(0, NodeIndex));
		}
	}

	while (!QueueToProcess.empty())
	{
		const std::pair<int32, int32> Elem = QueueToProcess.front();
		QueueToProcess.pop();

		int32 Level = Elem.first;
		int32 NodeIndex = Elem.second;
		const typename FConstraintGraph::FGraphNode& GraphNode = ConstraintGraph.Nodes[NodeIndex];

		for (int32 EdgeIndex : GraphNode.Edges)
		{
			const typename FConstraintGraph::FGraphEdge& GraphEdge = ConstraintGraph.Edges[EdgeIndex];
			FGraphEdgeColor& ColorEdge = Edges[EdgeIndex];

			// If this is not from our rule, ignore it
			if (GraphEdge.Data.ContainerId != ContainerId)
			{
				continue;
			}

			// If we have already been assigned a level, move on
			if (ColorEdge.Level >= 0)
			{
				continue;
			}

			// Does the node have edges that are not from this Island?
			// @todo(ccaulfield): look into this - this should never happen I think? it's an O(N) check
			if (!ConstraintDataIndices.Contains(EdgeIndex))
			{
				continue;
			}

			// Assign the level and update max level for the island if required
			ColorEdge.Level = Level;
			IslandData[Island].MaxLevel = FGenericPlatformMath::Max(IslandData[Island].MaxLevel, ColorEdge.Level);

			// Find adjacent node and recurse
			int32 OtherNode = INDEX_NONE;
			if (GraphEdge.FirstNode == NodeIndex)
			{
				OtherNode = GraphEdge.SecondNode;
			}
			if (GraphEdge.SecondNode == NodeIndex)
			{
				OtherNode = GraphEdge.FirstNode;
			}
			if (OtherNode != INDEX_NONE)
			{
				QueueToProcess.push(std::make_pair(ColorEdge.Level + 1, OtherNode));
			}
		}
	}

	// @todo(ccaulfield): What is this for? Isolated particles with no constraints? 
	// If so we should add them to some new islands, keeping the number of particles to each island in some reasonable range.
	// Answer: If an island is only dynamics the above code would be skipped. This simply adds them all to level 0
	for (const int32 EdgeIndex : ConstraintDataIndices)
	{
		check(Edges[EdgeIndex].Level <= IslandData[Island].MaxLevel);
		if (Edges[EdgeIndex].Level < 0)
		{
			Edges[EdgeIndex].Level = 0;
		}
	}

	check(IslandData[Island].MaxLevel >= 0 || !ConstraintDataIndices.Num());
}
#endif

template<typename T, int d>
void TPBDConstraintColor<T, d>::InitializeColor(const FConstraintGraph& ConstraintGraph)
{
	Nodes.Reset();
	Edges.Reset();
	IslandData.Reset();

	Nodes.SetNum(ConstraintGraph.Nodes.Num());
	Edges.SetNum(ConstraintGraph.Edges.Num());
	IslandData.SetNum(ConstraintGraph.IslandData.Num());
}

template<typename T, int d>
void TPBDConstraintColor<T, d>::ComputeColor(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const int32 Island, const FConstraintGraph& ConstraintGraph, uint32 ContainerId)
{
#ifdef USE_CONTACT_LEVELS
	ComputeContactGraph(InParticles, InIndices, Island, ConstraintGraph, ContainerId);
#endif
	ComputeIslandColoring(InParticles, InIndices, Island, ConstraintGraph, ContainerId);
}

template<typename T, int d>
const typename TPBDConstraintColor<T, d>::FLevelToColorToConstraintListMap& TPBDConstraintColor<T, d>::GetIslandLevelToColorToConstraintListMap(int32 Island) const
{
	if (Island < IslandData.Num())
	{
		return IslandData[Island].LevelToColorToConstraintListMap;
	}
	return EmptyLevelToColorToConstraintListMap;
}

template<typename T, int d>
int TPBDConstraintColor<T, d>::GetIslandMaxColor(int32 Island) const
{
	if (Island < IslandData.Num())
	{
		return IslandData[Island].MaxColor;
	}
	return -1;
}

template<typename T, int d>
int TPBDConstraintColor<T, d>::GetIslandMaxLevel(int32 Island) const
{
	if (Island < IslandData.Num())
	{
#ifdef USE_CONTACT_LEVELS
		return IslandData[Island].MaxLevel;
#else
		return 0;
#endif
	}
	return -1;
}

template class Chaos::TPBDConstraintColor<float, 3>;
