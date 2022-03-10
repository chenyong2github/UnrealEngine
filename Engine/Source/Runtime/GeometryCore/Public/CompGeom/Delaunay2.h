// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "IndexTypes.h"

#include "CoreMinimal.h"
#include "Templates/PimplPtr.h"

#include "Math/RandomStream.h"

#include "MathUtil.h"
#include "IndexTypes.h"
#include "PlaneTypes.h"
#include "LineTypes.h"
#include "Polygon2.h"


namespace UE {
namespace Geometry {

using namespace UE::Math;

// Internal representation of mesh connectivity; not exposed to interface
struct FDelaunay2Connectivity;

class GEOMETRYCORE_API FDelaunay2
{
public:
	//
	// Inputs
	//

	// Source for random permutations, used internally in the triangulation algorithm
	FRandomStream RandomStream;

	// Option to keep extra vertex->edge adjacency data; useful if you will call ConstrainEdges many times on the same triangulation
	bool bKeepFastEdgeAdjacencyData = false;

	// TODO: it would often be useful to pass in sparse vertex data
	//// Optional function to allow Triangulate to skip vertices
	//TFunction<bool(int32)> SkipVertexFn = nullptr;

	/**
	 * Compute an (optionally constrained) Delaunay triangulation
	 *
	 * @return false if triangulation failed
	 */
	bool Triangulate(TArrayView<const TVector2<double>> Vertices, TArrayView<const FIndex2i> Edges = TArrayView<const FIndex2i>());
	bool Triangulate(TArrayView<const TVector2<float>> Vertices, TArrayView<const FIndex2i> Edges = TArrayView<const FIndex2i>());

	/**
	 * Update an already-computed triangulation so the given edges are in the triangulation.
	 * Note: Assumes the edges do not intersect other constrained edges OR existing vertices in the triangulation
	 * TODO: track at least whether any easy-to-detect failures occur (at least an edge intersecting a vertex and never being inserted)
	 */
	void ConstrainEdges(TArrayView<const TVector2<double>> Vertices, TArrayView<const FIndex2i> Edges);
	void ConstrainEdges(TArrayView<const TVector2<float>> Vertices, TArrayView<const FIndex2i> Edges);

	// TODO: Support incremental vertex insertion
	// Update the triangulation incrementally, assuming Vertices are unchanged before FirstNewIndex, and nothing after FirstNewIndex has been inserted yet
	// Note that updating with new vertices *after* constraining edges may remove previously-constrained edges, unless we also add a way to tag constrained edges
	// bool Update(TArrayView<const TVector2<double>> Vertices, int32 FirstNewIdx);

	// Get the triangulation as an array of triangles
	// Note: This creates a new array each call, because the internal data structure does not have a triangle array
	TArray<FIndex3i> GetTriangles() const;

	// Get the triangulation as an array with a corresponding adjacency array, indicating the adjacent triangle on each triangle edge (-1 if no adjacent triangle)
	void GetTrianglesAndAdjacency(TArray<FIndex3i>& Triangles, TArray<FIndex3i>& Adjacency) const;

	// @return true if this is a constrained Delaunay triangulation
	bool IsConstrained() const
	{
		return bIsConstrained;
	}

	// @return true if triangulation is Delaunay, useful for validating results (note: likely to be false if edges are constrained)
	bool IsDelaunay(TArrayView<const FVector2f> Vertices) const;
	bool IsDelaunay(TArrayView<const FVector2d> Vertices) const;

protected:
	TPimplPtr<FDelaunay2Connectivity> Connectivity;

	bool bIsConstrained = false;
};

} // end namespace UE::Geometry
} // end namespace UE
