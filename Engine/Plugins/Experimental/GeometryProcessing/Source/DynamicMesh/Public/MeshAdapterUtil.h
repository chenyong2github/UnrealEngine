// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DynamicMesh3.h"
#include "PointSetAdapter.h"

/**
 * Utility functions for constructing PointSetAdapter instances from various 
 * point sets on a mesh
 */
namespace MeshAdapterUtil
{
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

