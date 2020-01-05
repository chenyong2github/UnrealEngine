// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"

/**
 * Utility functions for dealing with mesh indices
 */
namespace MeshIndexUtil
{
	/**
	 * Find list of unique vertices that are contained in one or more triangles
	 * @param Mesh input mesh
	 * @param TriangleIDs list of triangle IDs of Mesh
	 * @param VertexIDsOut list of vertices contained by triangles
	 */
	DYNAMICMESH_API void TriangleToVertexIDs(const FDynamicMesh3* Mesh, const TArray<int>& TriangleIDs, TArray<int>& VertexIDsOut);


	/**
	 * Find all the triangles in all the one rings of a set of vertices
	 * @param Mesh input mesh
	 * @param VertexIDs list of Vertex IDs of Mesh
	 * @param TriangleIDsOut list of triangle IDs where any vertex of triangle touches an element of VertexIDs
	 */
	DYNAMICMESH_API void VertexToTriangleOneRing(const FDynamicMesh3* Mesh, const TArray<int>& VertexIDs, TSet<int>& TriangleIDsOut);

}
