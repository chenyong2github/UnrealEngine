// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "EdgeSpan.h"
#include "Util/IndexUtil.h"
#include "Containers/BitArray.h"


/**
 * Given a per-triangle integer ("group"), FGroupTopology extracts a
 * group-level topological graph from an input Mesh. The graph consists
 * of three elements:
 *   Corners: there is a corner at each vertex where 3 or more groups meet (ie 3 or more groups in one-ring)
 *   Edges: a group edge is a list of one or more connected edges that have the same pair of different groups on either side
 *   Group: a group is a set of connected faces with the same GroupID
 *   
 * By default, the triangle group attribute of the input Mesh is used.
 * You can override GetGroupID to provide your own grouping.
 * 
 * Various query functions are provided to allow group topology to be interrogated.
 * Note that these functions refer to "GroupID", "CornerID", and "GroupEdgeID", 
 * these are simply indices into the internal .Groups, .Corners, and .Edges arrays
 */
class DYNAMICMESH_API FGroupTopology
{
public:
	FGroupTopology() {}
	FGroupTopology(const FDynamicMesh3* Mesh, bool bAutoBuild);

	virtual ~FGroupTopology() {}


	/**
	 * Build the group topology graph.
	 */
	void RebuildTopology();

	/** 
	 * Adjacency of Per-Triangle integers are what define the triangle groups.
	 * Override this function to provide an alternate group definition.
	 * @return group id integer for given TriangleID 
	 */
	virtual int GetGroupID(int TriangleID) const
	{
		return Mesh->GetTriangleGroup(TriangleID);
	}


	/**
	 * FCorner is a "corner" in the group topology, IE a vertex where 3+ groups meet.
	 */
	struct DYNAMICMESH_API FCorner
	{
		/** Mesh Vertex ID */
		int VertexID;		
		/** List of IDs of groups connected to this Corner */
		TArray<int> NeighbourGroupIDs;
	};

	/** List of Corners in the topology graph (ie these are the nodes/vertices of the graph) */
	TArray<FCorner> Corners;

	/** A Group is bounded by closed loops of FGroupEdge elements. A FGroupBoundary is one such loop. */
	struct DYNAMICMESH_API FGroupBoundary
	{
		/** Ordered list of edges forming this boundary */
		TArray<int> GroupEdges;
		/** List of IDs of groups on the "other side" of this boundary (this GroupBoundary is owned by a particular FGroup) */
		TArray<int> NeighbourGroupIDs;
		/** true if one or more edges in GroupEdges is on the mesh boundary */
		bool bIsOnBoundary;
	};

	/** FGroup is a set of connected triangles with the same GroupID */
	struct DYNAMICMESH_API FGroup
	{
		/** GroupID for this group */
		int GroupID;
		/** List of triangles forming this group */
		TArray<int> Faces;
		
		/** List of boundaries of this group (may be empty, eg on a closed component with only one group) */
		TArray<FGroupBoundary> Boundaries;
		/** List of groups that are adjacent to this group */
		TArray<int> NeighbourGroupIDs;
	};

	/** List of Groups in the topology graph (ie faces) */
	TArray<FGroup> Groups;


	/**
	 * FGroupEdge is a sequence of group-boundary-edges where the two groups on either side of each edge
	 * are the same. The sequence is stored an FEdgeSpan.
	 * 
	 * FGroupEdge instances are *shared* between the two FGroups on either side, via FGroupBoundary.
	 */
	struct DYNAMICMESH_API FGroupEdge
	{
		/** Identifier for this edge, as a pair of groups, sorted by increasing value */
		FIndex2i Groups;
		/** Edge span for this edge */
		FEdgeSpan Span;

		/** @return the member of .Groups that is not GroupID */
		int OtherGroupID(int GroupID) const 
		{ 
			check(Groups.A == GroupID || Groups.B == GroupID);
			return (Groups.A == GroupID) ? Groups.B : Groups.A; 
		}

		/** @return true if any vertex in the Span is in the Vertices set*/
		bool IsConnectedToVertices(const TSet<int>& Vertices) const;
	};

	/** List of Edges in the topology graph (each edge connects two corners/nodes) */
	TArray<FGroupEdge> Edges;



	/** @return the mesh vertex ID for the given Corner ID */
	int GetCornerVertexID(int CornerID) const;

	/** @return the FGroup for the given GroupID, or nullptr if not found */
	const FGroup* FindGroupByID(int GroupID) const;
	/** @return the list of triangles in the given GroupID, or empty list if not found */
	const TArray<int>& GetGroupFaces(int GroupID) const;
	/** @return the list of neighbour GroupIDs for the given GroupID, or empty list if not found */
	const TArray<int>& GetGroupNbrGroups(int GroupID) const;

	/** @return the ID of the FGroupEdge that contains the given Mesh Edge ID*/
	int FindGroupEdgeID(int MeshEdgeID) const;
	/** @return the list of vertices of a FGroupEdge identified by the GroupEdgeID */
	const TArray<int>& GetGroupEdgeVertices(int GroupEdgeID) const;

	/** Add the groups connected to the given GroupEdgeID to the GroupsOut list. This is not the either-side pair, but the set of groups on the one-ring of each connected corner. */
	void FindEdgeNbrGroups(int GroupEdgeID, TArray<int>& GroupsOut) const;
	/** Add the groups connected to all the GroupEdgeIDs to the GroupsOut list. This is not the either-side pair, but the set of groups on the one-ring of each connected corner. */
	void FindEdgeNbrGroups(const TArray<int>& GroupEdgeIDs, TArray<int>& GroupsOut) const;

	/** Add all the groups connected to the given Corner to the GroupsOut list */
	void FindCornerNbrGroups(int CornerID, TArray<int>& GroupsOut) const;
	/** Add all the groups connected to the given Corners to the GroupsOut list */
	void FindCornerNbrGroups(const TArray<int>& CornerIDs, TArray<int>& GroupsOut) const;

	/** Add all the groups connected to the given Mesh Vertex to the GroupsOut list */
	void FindVertexNbrGroups(int VertexID, TArray<int>& GroupsOut ) const;
	/** Add all the groups connected to the given Mesh Vertices to the GroupsOut list */
	void FindVertexNbrGroups(const TArray<int>& VertexIDs, TArray<int>& GroupsOut) const;

	/** Call EdgeFunc for each boundary edge of the given Group  (no order defined) */
	void ForGroupEdges(int GroupID, 
		const TFunction<void(const FGroupEdge& Edge, int EdgeIndex)>& EdgeFunc) const;

	/** Call EdgeFunc for each boundary edge of each of the given Groups (no order defined) */
	void ForGroupSetEdges(const TArray<int>& GroupIDs,
		const TFunction<void(const FGroupEdge& Edge, int EdgeIndex)>& EdgeFunc) const;

	/** Add all the vertices of the given GroupID to the Vertices set */
	void CollectGroupVertices(int GroupID, TSet<int>& Vertices) const;
	/** Add all the group boundary vertices of the given GroupID to the Vertices set */
	void CollectGroupBoundaryVertices(int GroupID, TSet<int>& Vertices) const;

	/** 
	 * Calculate tangent of group edge, as direction from start to end endpoints
	 * @return false if edge is degenerate or start == end 
	 */
	bool GetGroupEdgeTangent(int GroupEdgeID, FVector3d& TangentOut) const;

protected:
	const FDynamicMesh3* Mesh = nullptr;

	TArray<int> GroupIDToGroupIndexMap;		// allow fast lookup of index in .Groups, given GroupID
	TBitArray<> CornerVerticesFlags;		// bit array of corners for fast testing in ExtractGroupEdges
	TArray<int> EmptyArray;

	/** @return true if given mesh vertex is a Corner vertex */
	virtual bool IsCornerVertex(int VertexID) const;

	void ExtractGroupEdges(FGroup& Group);

	FIndex2i MakeEdgeID(int MeshEdgeID)
	{
		FIndex2i EdgeTris = Mesh->GetEdgeT(MeshEdgeID);

		if (EdgeTris.A == FDynamicMesh3::InvalidID)
		{
			return FIndex2i(GetGroupID(EdgeTris.B), FDynamicMesh3::InvalidGroupID);
		}
		else if (EdgeTris.B == FDynamicMesh3::InvalidID)
		{
			return FIndex2i(GetGroupID(EdgeTris.A), FDynamicMesh3::InvalidGroupID);
		}
		else
		{
			return MakeEdgeID(GetGroupID(EdgeTris.A), GetGroupID(EdgeTris.B));
		}
	}
	FIndex2i MakeEdgeID(int Group1, int Group2)
	{
		check(Group1 != Group2);
		check(Group1 >= 0 && Group2 >= 0);
		return (Group1 < Group2) ? FIndex2i(Group1, Group2) : FIndex2i(Group2, Group1);
	}

	int FindExistingGroupEdge(int GroupID, int OtherGroupID, int FirstVertexID);
};