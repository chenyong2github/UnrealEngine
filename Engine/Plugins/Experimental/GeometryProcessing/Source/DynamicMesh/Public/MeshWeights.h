// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp MeshWeights

#pragma once

#include "DynamicMesh3.h"

/**
 * FMeshWeights implements various techniques for computing local weights of a mesh,
 * for example one-ring weights like Cotangent or Mean-Value.
 */
class DYNAMICMESH_API FMeshWeights
{
public:

	/**
	 * Compute uniform centroid of a vertex one-ring.
	 * These weights are strictly positive and all equal to 1 / valence
	 */
	static FVector3d UniformCentroid(const FDynamicMesh3 & mesh, int VertexIndex);


	/**
	 * Compute mean-value centroid of a vertex one-ring.
	 * These weights are strictly positive.
	 */
	static FVector3d MeanValueCentroid(const FDynamicMesh3 & mesh, int VertexIndex);

	/**
	 * Compute cotan-weighted centroid of a vertex one-ring.
	 * These weights are numerically unstable if any of the triangles are degenerate.
	 * We catch these problems and return input vertex as centroid
	 */
	static FVector3d CotanCentroid(const FDynamicMesh3 & mesh, int VertexIndex);

	/**
	 * Compute the voronoi area associated with a vertex.
	 */
	static double VoronoiArea(const FDynamicMesh3& mesh, int VertexIndex);

protected:
	FMeshWeights() = delete;
};