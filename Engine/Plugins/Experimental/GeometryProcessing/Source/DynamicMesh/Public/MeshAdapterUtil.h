// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DynamicMesh3.h"
#include "PointSetAdapter.h"
#include "MeshAdapter.h"

/**
 * Utility functions for constructing various PointSetAdapter and MeshAdapter instances from dynamic meshes
 */
namespace MeshAdapterUtil
{

	/**
	 * @return Transformed adapter of a FDynamicMesh
	 */
	FTriangleMeshAdapterd DYNAMICMESH_API MakeTransformedDynamicMeshAdapter(const FDynamicMesh3* Mesh, FTransform Transform);

	/**
	 * @return 1:1 adapter of a FDynamicMesh; can be used as a starting point to create other adapters
	 */
	FTriangleMeshAdapterd DYNAMICMESH_API MakeDynamicMeshAdapter(const FDynamicMesh3* Mesh);

	/**
	 * @return Mesh vertices as a point set
	 */
	FPointSetAdapterd DYNAMICMESH_API MakeVerticesAdapter(const FDynamicMesh3* Mesh);

	/**
	 * @return Mesh triangle centroids as a point set
	 */
	FPointSetAdapterd DYNAMICMESH_API MakeTriCentroidsAdapter(const FDynamicMesh3* Mesh);

	/**
	 * @return mesh edge midpoints as a point set
	 */
	FPointSetAdapterd DYNAMICMESH_API MakeEdgeMidpointsAdapter(const FDynamicMesh3* Mesh);

	/**
	 * @return Mesh boundary edge midpoints as a point set
	 */
	FPointSetAdapterd DYNAMICMESH_API MakeBoundaryEdgeMidpointsAdapter(const FDynamicMesh3* Mesh);
}

