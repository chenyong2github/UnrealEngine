// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	/** A pair of int32s represent a directed edge. The first value represents 
	 * the source vertex, and the second value represents the destination 
	 * vertex.
	 */
	typedef TTuple<int32, int32> FTarjanEdge;

	/** A strongly connected component contains a subgraph of strongly connected
	 * vertices and their corresponding edges.
	 */
	struct FTarjanStronglyConnectedComponent
	{
		/** Vertices in the strongly connected component. */
		TArray<int32> Vertices;

		/** Edges in the strongly connected component. */
		TArray<FTarjanEdge> Edges;
	};

	/** A strongly connected component containing Metasound INodes and 
	 * FDataEdges.
	 */
	struct FMetasoundStronglyConnectedComponent
	{
		/** Nodes in the strongly connected component. */
		TArray<const INode*> Nodes;

		/** FDataEdges in the strongly connected component. */
		TArray<FDataEdge> Edges;
	};

	/** The Tarjan algorithm identifies strongly connected component in a 
	 * directed graph. Strongly connected components consist of a set vertices 
	 * where each vertex is reachable from every other vertex in the directed 
	 * graph. A strongly connected component of a directed graph generally 
	 * represents a cycle in the graph. Technically, a single vertex is 
	 * considered a strongly connected component, but for most use-cases that 
	 * information is not needed. By default, single vertices are not returned
	 * as strongly connected components.
	 */
	struct METASOUNDGRAPHCORE_API FTarjan
	{
		/** Find strongly connected components given a set of FTarjanEdges.
		 *
		 * @param InEdges - Edges in the directed graph.
		 * @param OutComponents - Strongly connected components found in the graph are be added to this array.
		 * @param bExcludeSingleVertex - If true, single vertices are not be returned as strongly connected components. If false, single vertices may be returned as strongly connected components. 
		 *
		 * @return True if one or more strongly connected components are added to OutComponents. False otherwise. 
		 */
		static bool FindStronglyConnectedComponents(const TSet<FTarjanEdge>& InEdges, TArray<FTarjanStronglyConnectedComponent>& OutComponents, bool bExcludeSingleVertex = true);

		/** Find strongly connected components given a Metasound IGraph.
		 *
		 * @param InGraph - Graph to be analyzed. 
		 * @param OutComponents - Strongly connected components found in the graph are be added to this array.
		 * @param bExcludeSingleVertex - If true, single vertices are not be returned as strongly connected components. If false, single vertices may be returned as strongly connected components. 
		 *
		 * @return True if one or more strongly connected components are added to OutComponents. False otherwise. 
		 */
		static bool FindStronglyConnectedComponents(const IGraph& InGraph, TArray<FMetasoundStronglyConnectedComponent>& OutComponents, bool bExcludeSingleVertex = true);
	};
}
