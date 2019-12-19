// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3sharp FaceGroupUtil

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshEditor.h"

/**
 * Utility functions for dealing with Mesh indices
 */
namespace FaceGroupUtil
{

	/**
	 * Set group ID of all triangles in Mesh
	 */
	DYNAMICMESH_API void SetGroupID(FDynamicMesh3& Mesh, int to)
	{
		if (Mesh.HasTriangleGroups() == false)
		{
			return;
		}
		for (int tid : Mesh.TriangleIndicesItr())
		{
			Mesh.SetTriangleGroup(tid, to);
		}
	}


	/**
	 * Set group id of subset of triangles in Mesh
	 */
	DYNAMICMESH_API void SetGroupID(FDynamicMesh3& Mesh, const TArrayView<const int>& triangles, int to)
	{
		if (Mesh.HasTriangleGroups() == false)
		{
			return;
		}
		for (int tid : triangles)
		{
			Mesh.SetTriangleGroup(tid, to);
		}
	}


	/**
	 * replace group id in Mesh
	 */
	DYNAMICMESH_API void SetGroupToGroup(FDynamicMesh3& Mesh, int from, int to)
	{
		if (Mesh.HasTriangleGroups() == false)
		{
			return;
		}

		int NT = Mesh.MaxTriangleID();
		for ( int tid = 0; tid < NT; ++tid)
		{
			if (Mesh.IsTriangle(tid))
			{
				int gid = Mesh.GetTriangleGroup(tid);
				if (gid == from)
				{
					Mesh.SetTriangleGroup(tid, to);
				}
			}
		}
	}


	/**
	 * find the set of group ids used in Mesh
	 */
	DYNAMICMESH_API TSet<int> FindAllGroups(const FDynamicMesh3& Mesh)
	{
		TSet<int> Groups;

		if (Mesh.HasTriangleGroups())
		{
			int NT = Mesh.MaxTriangleID();
			for (int tid = 0; tid < NT; ++tid)
			{
				if (Mesh.IsTriangle(tid))
				{
					int gid = Mesh.GetTriangleGroup(tid);
					Groups.Add(gid);
				}
			}
		}
		return Groups;
	}


	/**
	 * count number of tris in each group in Mesh; TODO: does this need sparse storage?
	 */
	DYNAMICMESH_API TArray<int> CountAllGroups(const FDynamicMesh3& Mesh)
	{
		TArray<int> GroupCounts; GroupCounts.SetNum(Mesh.MaxGroupID());

		if (Mesh.HasTriangleGroups())
		{
			int NT = Mesh.MaxTriangleID();
			for (int tid = 0; tid < NT; ++tid)
			{
				if (Mesh.IsTriangle(tid))
				{
					int gid = Mesh.GetTriangleGroup(tid);
					GroupCounts[gid]++;
				}
			}
		}
		return GroupCounts;
	}


	/**
	 * collect triangles by group id. Returns array of triangle lists (stored as arrays).
	 * This requires 2 passes over Mesh, but each pass is linear
	 */
	DYNAMICMESH_API TArray<TArray<int>> FindTriangleSetsByGroup(const FDynamicMesh3& Mesh, int IgnoreGID = -1)
	{
		TArray<TArray<int>> Sets;
		if (!Mesh.HasTriangleGroups())
		{
			return Sets;
		}

		// find # of groups and triangle count for each
		TArray<int> Counts = CountAllGroups(Mesh);
		TArray<int> GroupIDs;
		for (int CountIdx = 0; CountIdx < Counts.Num(); CountIdx++) {
			int Count = Counts[CountIdx];
			if (CountIdx != IgnoreGID && Count > 0)
			{
				GroupIDs.Add(CountIdx);
			}
		}
		TArray<int> GroupMap; GroupMap.SetNum(Mesh.MaxGroupID());

		// allocate sets
		Sets.SetNum(GroupIDs.Num());
		for (int i = 0; i < GroupIDs.Num(); ++i)
		{
			int GID = GroupIDs[i];
			Sets[i].Reserve(Counts[GID]);
			GroupMap[GID] = i;
		}

		// accumulate triangles
		int NT = Mesh.MaxTriangleID();
		for (int tid = 0; tid < NT; ++tid)
		{
			if (Mesh.IsTriangle(tid))
			{
				int GID = Mesh.GetTriangleGroup(tid);
				int i = GroupMap[GID];
				if (i >= 0)
				{
					Sets[i].Add(tid);
				}
			}
		}

		return Sets;
	}



	/**
	 * find list of triangles in Mesh with specific group id
	 */
	DYNAMICMESH_API TArray<int> FindTrianglesByGroup(FDynamicMesh3& Mesh, int FindGroupID)
	{
		TArray<int> tris;
		if (Mesh.HasTriangleGroups() == false)
		{
			return tris;
		}
		for (int tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(tid) == FindGroupID)
			{
				tris.Add(tid);
			}
		}
		return tris;
	}

	/**
	* split input Mesh into submeshes based on group ID
	* **does not** separate disconnected components w/ same group ID
	*/
	DYNAMICMESH_API void SeparateMeshByGroups(FDynamicMesh3& Mesh, TArray<FDynamicMesh3>& SplitMeshes)
	{
		FDynamicMeshEditor::SplitMesh(&Mesh, SplitMeshes, [&Mesh](int TID)
		{
			return Mesh.GetTriangleGroup(TID);
		});
	}

	/**
	* split input Mesh into submeshes based on group ID
	* **does not** separate disconnected components w/ same group ID
	*/
	DYNAMICMESH_API void SeparateMeshByGroups(FDynamicMesh3& Mesh, TArray<FDynamicMesh3>& SplitMeshes, TArray<int>& GroupIDs)
	{
		// build split meshes
		SeparateMeshByGroups(Mesh, SplitMeshes);

		// build array of per-mesh group id
		GroupIDs.Reset();
		for (const FDynamicMesh3& M : SplitMeshes)
		{
			check(M.TriangleCount() > 0); // SplitMesh should never add an empty mesh
			GroupIDs.Add(M.GetTriangleGroup(0));
		}
	}


}
