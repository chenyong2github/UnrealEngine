// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"

/**
 * Utility functions for dealing with mesh indices
 */
namespace MeshIndexUtil
{
	//
	// Do not add any more functions to this namespace. Use UE::MeshIndexUtil instead
	//

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

namespace UE
{
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

		/**
		 * Walk around VertexID from FromTriangleID to next connected triangle if it exists, walking "away" from PrevTriangleID.
		 * @param TrisConnectedFunc returns true if two triangles should be considered connected, to support breaking at seams/etc that are not in base mesh topology
		 * @return triplet of values (FoundTriangleID, SharedEdgeID, IndexOfEdgeInFromTri), or all IndexConstants::InvalidID if not found
		 */
		template<typename TrisConnectedPredicate>
		FIndex3i FindNextAdjacentTriangleAroundVtx(const FDynamicMesh3* Mesh, 
			int32 VertexID, int32 FromTriangleID, int32 PrevTriangleID,
			TrisConnectedPredicate TrisConnectedTest)
		{
			check(Mesh);

			// find neigbour edges and tris for triangle
			FIndex3i TriEdges = Mesh->GetTriEdges(FromTriangleID);
			FIndex3i TriNbrTris;
			for (int32 j = 0; j < 3; ++j)
			{
				FIndex2i EdgeT = Mesh->GetEdgeT(TriEdges[j]);
				TriNbrTris[j] = (EdgeT.A == FromTriangleID) ? EdgeT.B : EdgeT.A;
			}

			// Search for the neighbour tri that is not PrevTriangleID, and is also connected to VertexID.
			// This is our next triangle around the ring
			for (int32 j = 0; j < 3; ++j)
			{
				if (TriNbrTris[j] != PrevTriangleID && Mesh->IsTriangle(TriNbrTris[j]))
				{
					FIndex3i TriVerts = Mesh->GetTriangle(TriNbrTris[j]);
					if (TriVerts.A == VertexID || TriVerts.B == VertexID || TriVerts.C == VertexID)
					{
						// test if predicate allows this connection
						if (TrisConnectedTest(FromTriangleID, TriNbrTris[j], TriEdges[j]) == false)
						{
							break;
						}
						return FIndex3i(TriNbrTris[j], TriEdges[j], j);
					}
				}
			}

			return FIndex3i(IndexConstants::InvalidID, IndexConstants::InvalidID, IndexConstants::InvalidID);
		}
	}
}
