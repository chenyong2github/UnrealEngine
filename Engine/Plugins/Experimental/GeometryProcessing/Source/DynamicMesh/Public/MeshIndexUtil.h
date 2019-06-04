// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"

/**
 * Utility functions for dealing with mesh indices
 */
namespace MeshIndexUtil
{
	/**
	 * @param Mesh input mesh
	 * @param TriangleIDs list of triangle IDs of Mesh
	 * @param VertexIDsOut list of vertices contained by triangles
	 */
	DYNAMICMESH_API void ConverTriangleToVertexIDs(const FDynamicMesh3* Mesh, const TArray<int>& TriangleIDs, TArray<int>& VertexIDsOut);


}
