// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

namespace CADKernel
{
	class FSession;
	class FShell;
	class FTopologicalEdge;
	class FTopologicalFace;
	class FTopologicalVertex;

	class FJoiner
	{
		friend class FThinZone;
		friend class FThinZoneFinder;

	protected:

		TSharedRef<FSession> Session;

		TArray<TSharedPtr<FShell>> Shells;
		TArray<TSharedPtr<FTopologicalFace>> Faces;

		double JoiningTolerance;
		double JoiningToleranceSquare;

	public:

		FJoiner(TSharedRef<FSession> InSession, const TArray<TSharedPtr<FShell>>& Shells, double Tolerance);
		FJoiner(TSharedRef<FSession> InSession, const TArray<TSharedPtr<FTopologicalFace>>& Surfaces, double Tolerance);

		void JoinFaces();
		//void JoinFaces(bool bProcessOnlyBorderEdges, bool bProcessOnlyNonManifoldEdges);

		/**
		 * Check topology of each body
		 */
		void CheckTopology();

		//void MergeInto(TSharedPtr<FBody> Body, TArray<TSharedPtr<FTopologicalEntity>>& InEntities);
		//void SortByShell(TSharedPtr<FBody> Body, TArray<TSharedPtr<FBody>>& OutNewBody);
		//void Join(TArray<TSharedPtr<FBody>> Bodies, double Tolerance);

		void SplitIntoConnectedShell();
		void RemoveFacesFromShell();

	private:

		void EmptyShells();

		/**
		 * Return an array of active vertices.
		 */
		void GetVertices(TArray<TSharedPtr<FTopologicalVertex>>& Vertices);

		/**
		 * Return an array of active border vertices.
		 */
		void GetBorderVertices(TArray<TSharedPtr<FTopologicalVertex>>& BorderVertices);

		/**
		 * Merge Border Vertices with other vertices.
		 * @param Vertices: the initial array of active vertices, this array is updated at the end of the process
		 */
		void MergeCoincidentVertices(TArray<TSharedPtr<FTopologicalVertex>>& VerticesToMerge);

		/**
		 * Merge Border Vertices with other vertices.
		 * @param Vertices: the initial array of active vertices, this array is updated at the end of the process
		 */
		void MergeBorderVerticesWithCoincidentOtherVertices(TArray<TSharedPtr<FTopologicalVertex>>& Vertices);

		//void FixCollapsedEdges();
		void CheckSelfConnectedEdge();
		void RemoveIsolatedEdges(); // Usefull ???

		/**
		 * First step, trivial edge merge i.e. couple of edges with same extremity vertex
		 */
		void MergeCoincidentEdges(TArray<TSharedPtr<FTopologicalVertex>>& Vertices);

		/**
		 * Second step, parallel edges but with different length. the longest must be split
		 */
		void StitchParallelEdges(TArray<TSharedPtr<FTopologicalVertex>>& Vertices);

		/**
		 * Split the edge to split except if one side is too small (JoiningTolerance). In this case (too small), the EdgeToSplit is not split but linked to EdgeToLink. 
		 * @return the created vertex or TSharedPtr<FTopologicalVertex>()
		 */
		TSharedPtr<FTopologicalVertex> SplitAndLink(TSharedRef<FTopologicalVertex>& StartVertex, TSharedPtr<FTopologicalEdge>& EdgeToLink, TSharedPtr<FTopologicalEdge>& EdgeToSplit);

		/**
		 * For each loop of each surface, check if successive edges are unconnected and if their common vertices are connected only to them. 
		 * These edges are merged into one edge. 
		 * E.g. :
		 * Face A has 3 successive unconnected edges. If these 3 edges are merged to give only one edge, the new edge could be linked to its parallel edge of Face B 
		 * 
		 *              \                          Face A                                   |    Face C
		 *               \                                                                  |
		 *    Face E     CV ------------------- UV ------------------ UV ----------------- CV --------------------
		 *               CV -------------------------------------------------------------- CV --------------------
		 *               /                         Face B                                   |    Face D
		 *              /                                                                   |
		 */
		void MergeUnconnectedAdjacentEdges();

	};

} // namespace CADKernel
