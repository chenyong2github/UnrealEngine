// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "DynamicMesh3.h"

/**
 * Calculate Convex Hull of a Mesh
 */
class DYNAMICMESH_API FMeshConvexHull
{
public:
	/** Input Mesh */
	const FDynamicMesh3* Mesh;

	/** If set, hull will be computed on subset of vertices */
	TArray<int32> VertexSet;

	/** If true, output convex hull is simplified down to MaxTargetFaceCount */
	bool bPostSimplify = false;
	/** Target triangle count of the output Convex Hull */
	int32 MaxTargetFaceCount = 0;

	/** Output convex hull */
	FDynamicMesh3 ConvexHull;


public:
	FMeshConvexHull(const FDynamicMesh3* MeshIn)
	{
		Mesh = MeshIn;
	}

	/**
	 * Calculate output ConvexHull mesh for vertices of input Mesh
	 * @return true on success
	 */
	bool Compute();



protected:
	bool Compute_FullMesh();
	bool Compute_VertexSubset();
};