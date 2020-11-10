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

		/**
		 * Initialize the color structures based on the connectivity graph (i.e., reset all color-related node, edge and island data).
		 */
		void InitializeColor(const FPBDConstraintGraph& ConstraintGraph);

		/**
		 * Calculate the color information for the specified island.
		 */
		void ComputeColor(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId);

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

	private:
		void ComputeContactGraph(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId);
		void ComputeIslandColoring(const int32 Island, const FPBDConstraintGraph& ConstraintGraph, uint32 ContainerId);

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
			{
			}
			int32 MaxColor;
			int32 MaxLevel;
			FLevelToColorToConstraintListMap LevelToColorToConstraintListMap;
		};

		TArray<FGraphNodeColor> Nodes;
		TArray<FGraphEdgeColor> Edges;
		TArray<FIslandColor> IslandData;
		FLevelToColorToConstraintListMap EmptyLevelToColorToConstraintListMap;
		TArray<int32> UpdatedNodes;
		bool bUseContactGraph;
	};

}
