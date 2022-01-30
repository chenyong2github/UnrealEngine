// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

#include "CADKernel/Topo/TopomakerReport.h"

namespace CADKernel
{

class FModel;
class FSession;
class FShell;
class FTopologicalEdge;
class FTopologicalFace;
class FTopologicalVertex;

class CADKERNEL_API FTopomaker
{
	friend class FThinZone;
	friend class FThinZoneFinder;

protected:

	FSession& Session;

	TArray<FShell*> Shells;
	TArray<TSharedPtr<FTopologicalFace>> Faces;

	const double SewTolerance;
	const double SewToleranceSquare;

	FTopomakerReport Report;

public:

	FTopomaker(FSession& InSession, double Tolerance);
	FTopomaker(FSession& InSession, const TArray<TSharedPtr<FShell>>& Shells, double Tolerance);
	FTopomaker(FSession& InSession, const TArray<TSharedPtr<FTopologicalFace>>& Surfaces, double Tolerance);

	void Sew();

	/**
	 * Check topology of each body
	 */
	void CheckTopology();

	//void MergeInto(TSharedPtr<FBody> Body, TArray<TSharedPtr<FTopologicalEntity>>& InEntities);
	//void SortByShell(TSharedPtr<FBody> Body, TArray<TSharedPtr<FBody>>& OutNewBody);
	//void Join(TArray<TSharedPtr<FBody>> Bodies, double Tolerance);

	/**
	 * Split into connected shell and put each shell into the appropriate body
	 */
	void SplitIntoConnectedShells();

	void OrientShells();

	/**
	 * Unlink Non-Manifold Vertex i.e. Vertex belong to tow or more shell
	 */
	void UnlinkNonManifoldVertex();

	void PrintReport()
	{
		Report.Print();
	}

private:

	/**
	 * Call by constructor.
	 * For each shell, add their faces into Faces array, complete the metadata and set the states for the joining process
	 */
	void InitFaces();

	void EmptyShells();

	void RemoveFacesFromShell();

	void RemoveEmptyShells();

	/**
	 * Return an array of active vertices.
	 */
	void GetVertices(TArray<FTopologicalVertex*>& Vertices);

	/**
	 * Return an array of active border vertices.
	 */
	void GetBorderVertices(TArray<FTopologicalVertex*>& BorderVertices);

	/**
	 * Merge Border Vertices with other vertices.
	 * @param Vertices: the initial array of active vertices, this array is updated at the end of the process
	 */
	void MergeCoincidentVertices(TArray<FTopologicalVertex*>& VerticesToMerge);

	/**
	 * Merge Border Vertices with other vertices.
	 * @param Vertices: the initial array of active vertices, this array is updated at the end of the process
	 */
	void MergeBorderVerticesWithCoincidentOtherVertices(TArray<FTopologicalVertex*>& Vertices);

	//void FixCollapsedEdges();
	void CheckSelfConnectedEdge();
	void RemoveIsolatedEdges(); // Useful ???

	/**
	 * First step, trivial edge merge i.e. couple of edges with same extremity vertex
	 */
	void MergeCoincidentEdges(TArray<FTopologicalVertex*>& Vertices);

	/**
	 * Second step, parallel edges but with different length. the longest must be split
	 */
	void StitchParallelEdges(TArray<FTopologicalVertex*>& Vertices);

	/**
	 * Split the edge to split except if one side is too small (JoiningTolerance). In this case (too small), the EdgeToSplit is not split but linked to EdgeToLink.
	 * @return the created vertex or TSharedPtr<FTopologicalVertex>()
	 */
	TSharedPtr<FTopologicalVertex> SplitAndLink(FTopologicalVertex& StartVertex, FTopologicalEdge& EdgeToLink, FTopologicalEdge& EdgeToSplit);

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
