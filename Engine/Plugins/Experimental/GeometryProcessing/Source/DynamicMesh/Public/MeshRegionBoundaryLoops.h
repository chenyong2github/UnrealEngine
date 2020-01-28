// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp MeshRegionBoundaryLoops

#pragma once

#include "DynamicMesh3.h"
#include "EdgeLoop.h"
#include "Util/SparseIndexCollectionTypes.h"

/**
 * Extract FEdgeLoops on the boundary of a set of triangles of a mesh.
 * @todo this was an early port and possibly can be optimized
 */
class DYNAMICMESH_API FMeshRegionBoundaryLoops
{
public:
	/** Mesh we are finding loops on */
	const FDynamicMesh3* Mesh = nullptr;
	/** Resulting set of loops filled by Compute() */
	TArray<FEdgeLoop> Loops;

public:
	FMeshRegionBoundaryLoops() {}

	FMeshRegionBoundaryLoops(const FDynamicMesh3* MeshIn, const TArray<int>& RegionTris, bool bAutoCompute = true);

	/**
	 * Find set of FEdgeLoops on the border of the input triangle set
	 * @return false if errors occurred, in this case output set is incomplete
	 */
	bool Compute();


	/** @return number of loops found by Compute() */
	int GetLoopCount() const
	{
		return Loops.Num();
	}

	/** @return Loop at the given index */
	const FEdgeLoop& operator[](int Index) const
	{
		return Loops[Index];
	}

	/** @return index of loop with maximum number of vertices */
	int GetMaxVerticesLoopIndex() const;
	


protected:

	// sets of included triangles and edges
	FIndexFlagSet Triangles;
	FIndexFlagSet Edges;
	TArray<int> edges_roi;

	bool IsEdgeOnBoundary(int eid) const { return Edges.Contains(eid); }

	// returns true for both internal and mesh boundary edges
	// tid_in and tid_out are triangles 'in' and 'out' of set, respectively
	bool IsEdgeOnBoundary(int eid, int& tid_in, int& tid_out) const;

	// return same indices as GetEdgeV, but oriented based on attached triangle
	FIndex2i GetOrientedEdgeVerts(int eID, int tid_in, int tid_out);

	// returns first two boundary edges, and count of total boundary edges
	int GetVertexBoundaryEdges(int vID, int& e0, int& e1);

	// e needs to be large enough (ie call GetVtxBoundaryEdges, or as large as max one-ring)
	// returns count, ie number of elements of e that were filled
	int GetAllVertexBoundaryEdges(int vID, TArray<int>& e);


	// [TODO] cache this : a dictionary? we will not need very many, but we will
	//   need each multiple times!
	FVector3d GetVertexNormal(int vid);

	// ok, bdry_edges[0...bdry_edges_count] contains the boundary edges coming out of bowtie_v.
	// We want to pick the best one to continue the loop that came : to bowtie_v on incoming_e.
	// If the loops are all sane, then we will get the smallest loops by "turning left" at bowtie_v.
	int FindLeftTurnEdge(int incoming_e, int bowtie_v, TArray<int>& bdry_edges, int bdry_edges_count, const FIndexFlagSet& used_edges);


	// This is called when loopV contains one or more "bowtie" vertices.
	// These vertices *might* be duplicated : loopV (but not necessarily)
	// If they are, we have to break loopV into subloops that don't contain duplicates.
	TArray<FEdgeLoop> ExtractSubloops(TArray<int>& loopV, const TArray<int>& loopE, const TArray<int>& bowties);


};
