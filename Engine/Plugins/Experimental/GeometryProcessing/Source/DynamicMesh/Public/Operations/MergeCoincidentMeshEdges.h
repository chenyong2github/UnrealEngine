// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MergeCoincidentEdges

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"

class FDynamicMesh3;

/**
 * FMergeCoincidentMeshEdges finds pairs of boundary edges of the mesh that are identical 
 * (ie have endpoint vertices at the same locations) and merges the pair into a single edge.
 * This is similar to welding vertices but safer because it prevents bowties from being formed.
 *
 * Currently if the two edges have the same "orientation" (ie from their respective triangles)
 * they cannot be merged. 
 *
 */
class DYNAMICMESH_API FMergeCoincidentMeshEdges
{
public:
	/** default tolerance is float ZeroTolerance */
	static const double DEFAULT_TOLERANCE;  // = FMathf::ZeroTolerance;

	/** The mesh that we are modifying */
	FDynamicMesh3* Mesh;

	/** Edges are coincident if both pairs of endpoint vertices are closer than this distance */
	double MergeVertexTolerance = DEFAULT_TOLERANCE;

	/** Only merge unambiguous pairs that have unique duplicate-edge matches */
	bool OnlyUniquePairs = false;

	/** 
	  * Edges are considered as potentially the same if their midpoints are within this distance.
	  * Due to floating-point roundoff this should be larger than MergeVertexTolerance.
	  * If zero, we set to MergeVertexTolerance*2
	  */
	double MergeSearchTolerance = 0;

public:
	FMergeCoincidentMeshEdges(FDynamicMesh3* mesh) : Mesh(mesh)
	{
	}

	/**
	 * Run the merge operation and modify .Mesh
	 * @return true if the algorithm succeeds
	 */
	virtual bool Apply();


protected:
	double MergeVtxDistSqr;		// cached value

	// returns true if endpoint vertices are within tolerance. Note that we do not know the order of
	// the vertices here so we try both combinations.
	inline bool IsSameEdge(const FVector3d& a, const FVector3d& b, const FVector3d& c, const FVector3d& d) const
	{
		return (a.DistanceSquared(c) < MergeVtxDistSqr && b.DistanceSquared(d) < MergeVtxDistSqr) ||
			(a.DistanceSquared(d) < MergeVtxDistSqr && b.DistanceSquared(c) < MergeVtxDistSqr);
	}
};