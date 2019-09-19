// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GroupTopology.h"
#include "MeshRegionBoundaryLoops.h"





bool FGroupTopology::FGroupEdge::IsConnectedToVertices(const TSet<int>& Vertices) const
{
	for (int VertexID : Span.Vertices)
	{
		if (Vertices.Contains(VertexID))
		{
			return true;
		}
	}
	return false;
}



FGroupTopology::FGroupTopology(const FDynamicMesh3* MeshIn, bool bAutoBuild)
{
	this->Mesh = MeshIn;
	if (bAutoBuild)
	{
		RebuildTopology();
	}
}


void FGroupTopology::RebuildTopology()
{
	Groups.Reset();
	Edges.Reset();
	Corners.Reset();

	// initialize groups map first to avoid resizes
	GroupIDToGroupIndexMap.Reset();
	GroupIDToGroupIndexMap.Init(-1, Mesh->MaxGroupID());
	TArray<int> GroupFaceCounts;
	GroupFaceCounts.Init(0, Mesh->MaxGroupID());
	for (int tid : Mesh->TriangleIndicesItr())
	{
		int GroupID = GetGroupID(tid);
		if (GroupIDToGroupIndexMap[GroupID] == -1)
		{
			FGroup NewGroup;
			NewGroup.GroupID = GroupID;
			GroupIDToGroupIndexMap[GroupID] = Groups.Add(NewGroup);
		}
		GroupFaceCounts[GroupID]++;
	}
	for (FGroup& Group : Groups)
	{
		Group.Faces.Reserve(GroupFaceCounts[Group.GroupID]);
	}


	// sort faces into groups
	for (int tid : Mesh->TriangleIndicesItr())
	{
		int GroupID = GetGroupID(tid);
		Groups[GroupIDToGroupIndexMap[GroupID]].Faces.Add(tid);
	}

	// precompute junction vertices set
	CornerVerticesFlags.Init(false, Mesh->MaxVertexID());
	for (int vid : Mesh->VertexIndicesItr())
	{
		if (IsCornerVertex(vid))
		{
			CornerVerticesFlags[vid] = true;
			FCorner Corner = { vid };
			Corners.Add(Corner);
		}
	}
	for (FCorner& Corner : Corners)
	{
		Mesh->GetAllVertexGroups(Corner.VertexID, Corner.NeighbourGroupIDs);
	}


	// construct boundary loops
	for (FGroup& Group : Groups)
	{
		// finds FGroupEdges and uses to populate Group.Boundaries
		ExtractGroupEdges(Group);

		// collect up .NeighbourGroupIDs and set .bIsOnBoundary
		for (FGroupBoundary& Boundary : Group.Boundaries)
		{
			Boundary.bIsOnBoundary = false;
			for (int EdgeIndex : Boundary.GroupEdges)
			{
				FGroupEdge& Edge = Edges[EdgeIndex];

				int OtherGroupID = (Edge.Groups.A == Group.GroupID) ? Edge.Groups.B : Edge.Groups.A;
				if (OtherGroupID != FDynamicMesh3::InvalidID)
				{
					Boundary.NeighbourGroupIDs.AddUnique(OtherGroupID);
				}
				else
				{
					Boundary.bIsOnBoundary = true;
				}
			}
		}

		// make all-neighbour-groups list at group level
		for (FGroupBoundary& Boundary : Group.Boundaries)
		{
			for (int NbrGroupID : Boundary.NeighbourGroupIDs)
			{
				Group.NeighbourGroupIDs.AddUnique(NbrGroupID);
			}
		}
	}

}


bool FGroupTopology::IsCornerVertex(int VertexID) const
{
	FIndex3i UniqueGroups;
	int UniqueCount = 0;
	for (int tid : Mesh->VtxTrianglesItr(VertexID))
	{
		int GroupID = GetGroupID(tid);
		if (UniqueCount == 0)
		{
			UniqueGroups[0] = GroupID;
			UniqueCount++;
		}
		else if (UniqueCount == 1 && GroupID != UniqueGroups[0])
		{
			UniqueGroups[1] = GroupID;
			UniqueCount++;
		}
		else if (UniqueCount == 2 && GroupID != UniqueGroups[0] && GroupID != UniqueGroups[1])
		{
			return true;
		}
	}
	return false;
}




int FGroupTopology::GetCornerVertexID(int CornerID) const
{
	check(CornerID >= 0 && CornerID < Corners.Num());
	return Corners[CornerID].VertexID;
}


const FGroupTopology::FGroup* FGroupTopology::FindGroupByID(int GroupID) const
{
	if (GroupID < 0 || GroupID >= GroupIDToGroupIndexMap.Num() || GroupIDToGroupIndexMap[GroupID] == -1)
	{
		return nullptr;
	}
	return &Groups[GroupIDToGroupIndexMap[GroupID]];
}


const TArray<int>& FGroupTopology::GetGroupFaces(int GroupID) const
{
	const FGroup* Found = FindGroupByID(GroupID);
	ensure(Found != nullptr);
	return (Found != nullptr) ? Found->Faces : EmptyArray;
}

const TArray<int>& FGroupTopology::GetGroupNbrGroups(int GroupID) const
{
	const FGroup* Found = FindGroupByID(GroupID);
	ensure(Found != nullptr);
	return (Found != nullptr) ? Found->NeighbourGroupIDs : EmptyArray;
}



int FGroupTopology::FindGroupEdgeID(int MeshEdgeID) const
{
	int GroupID = GetGroupID(Mesh->GetEdgeT(MeshEdgeID).A);
	const FGroup* Group = FindGroupByID(GroupID);
	ensure(Group != nullptr);
	if (Group != nullptr)
	{
		for (const FGroupBoundary& Boundary : Group->Boundaries)
		{
			for (int EdgeID : Boundary.GroupEdges)
			{
				const FGroupEdge& Edge = Edges[EdgeID];
				if (Edge.Span.Edges.Contains(MeshEdgeID))
				{
					return EdgeID;
				}
			}
		}
	}
	return -1;
}


const TArray<int>& FGroupTopology::GetGroupEdgeVertices(int GroupEdgeID) const
{
	check(GroupEdgeID >= 0 && GroupEdgeID < Edges.Num());
	return Edges[GroupEdgeID].Span.Vertices;
}



void FGroupTopology::FindEdgeNbrGroups(int GroupEdgeID, TArray<int>& GroupsOut) const
{
	check(GroupEdgeID >= 0 && GroupEdgeID < Edges.Num());
	const TArray<int> & Vertices = GetGroupEdgeVertices(GroupEdgeID);
	FindVertexNbrGroups(Vertices[0], GroupsOut);
	FindVertexNbrGroups(Vertices[Vertices.Num() - 1], GroupsOut);
}

void FGroupTopology::FindEdgeNbrGroups(const TArray<int>& GroupEdgeIDs, TArray<int>& GroupsOut) const
{
	for (int GroupEdgeID : GroupEdgeIDs)
	{
		FindEdgeNbrGroups(GroupEdgeID, GroupsOut);
	}
}



void FGroupTopology::FindCornerNbrGroups(int CornerID, TArray<int>& GroupsOut) const
{
	check(CornerID >= 0 && CornerID < Corners.Num());
	for (int GroupID : Corners[CornerID].NeighbourGroupIDs)
	{
		GroupsOut.AddUnique(GroupID);
	}
}

void FGroupTopology::FindCornerNbrGroups(const TArray<int>& CornerIDs, TArray<int>& GroupsOut) const
{
	for (int cid : CornerIDs)
	{
		FindCornerNbrGroups(cid, GroupsOut);
	}
}




void FGroupTopology::FindVertexNbrGroups(int VertexID, TArray<int>& GroupsOut) const
{
	for (int tid : Mesh->VtxTrianglesItr(VertexID))
	{
		int GroupID = GetGroupID(tid);
		GroupsOut.AddUnique(GroupID);
	}
}

void FGroupTopology::FindVertexNbrGroups(const TArray<int>& VertexIDs, TArray<int>& GroupsOut) const
{
	for (int vid : VertexIDs)
	{
		for (int tid : Mesh->VtxTrianglesItr(vid))
		{
			int GroupID = GetGroupID(tid);
			GroupsOut.AddUnique(GroupID);
		}
	}
}



void FGroupTopology::CollectGroupVertices(int GroupID, TSet<int>& Vertices) const
{
	const FGroup* Found = FindGroupByID(GroupID);
	ensure(Found != nullptr);
	if (Found != nullptr)
	{
		for (int TriID : Found->Faces)
		{
			FIndex3i TriVerts = Mesh->GetTriangle(TriID);
			Vertices.Add(TriVerts.A);
			Vertices.Add(TriVerts.B);
			Vertices.Add(TriVerts.C);
		}
	}
}



void FGroupTopology::CollectGroupBoundaryVertices(int GroupID, TSet<int>& Vertices) const
{
	const FGroup* Group = FindGroupByID(GroupID);
	ensure(Group != nullptr);
	if (Group != nullptr)
	{
		for (const FGroupBoundary& Boundary : Group->Boundaries)
		{
			for (int EdgeIndex : Boundary.GroupEdges)
			{
				const FGroupEdge& Edge = Edges[EdgeIndex];
				for (int vid : Edge.Span.Vertices)
				{
					Vertices.Add(vid);
				}
			}
		}
	}
}





void FGroupTopology::ForGroupEdges(int GroupID,
	const TFunction<void(const FGroupEdge& Edge, int EdgeIndex)>& EdgeFunc) const
{
	const FGroup* Group = FindGroupByID(GroupID);
	ensure(Group != nullptr);
	if (Group != nullptr)
	{
		for (const FGroupBoundary& Boundary : Group->Boundaries)
		{
			for (int EdgeIndex : Boundary.GroupEdges)
			{
				EdgeFunc(Edges[EdgeIndex], EdgeIndex);
			}
		}
	}
}



void FGroupTopology::ForGroupSetEdges(const TArray<int>& GroupIDs,
	const TFunction<void(const FGroupEdge& Edge, int EdgeIndex)>& EdgeFunc) const
{
	TArray<int> DoneEdges;

	for (int GroupID : GroupIDs)
	{
		const FGroup* Group = FindGroupByID(GroupID);
		ensure(Group != nullptr);
		if (Group != nullptr)
		{
			for (const FGroupBoundary& Boundary : Group->Boundaries)
			{
				for (int EdgeIndex : Boundary.GroupEdges)
				{
					if (DoneEdges.Contains(EdgeIndex) == false)
					{
						EdgeFunc(Edges[EdgeIndex], EdgeIndex);
						DoneEdges.Add(EdgeIndex);
					}
				}
			}
		}
	}
}




void FGroupTopology::ExtractGroupEdges(FGroup& Group)
{
	FMeshRegionBoundaryLoops BdryLoops(Mesh, Group.Faces, true);
	int NumLoops = BdryLoops.Loops.Num();

	Group.Boundaries.SetNum(NumLoops);
	for ( int li = 0; li < NumLoops; ++li )
	{
		FEdgeLoop& Loop = BdryLoops.Loops[li];
		FGroupBoundary& Boundary = Group.Boundaries[li];

		// find indices of corners of group polygon
		TArray<int> CornerIndices;
		int NumV = Loop.Vertices.Num();
		for (int i = 0; i < NumV; ++i)
		{
			if (CornerVerticesFlags[Loop.Vertices[i]])
			{
				CornerIndices.Add(i);
			}
		}

		// if we had no indices then this is like the cap of a cylinder, just one single long edge
		if ( CornerIndices.Num() == 0 )
		{ 
			FIndex2i EdgeID = MakeEdgeID(Loop.Edges[0]);
			int OtherGroupID = (EdgeID.A == Group.GroupID) ? EdgeID.B : EdgeID.A;
			int EdgeIndex = FindExistingGroupEdge(Group.GroupID, OtherGroupID, Loop.Vertices[0]);
			if (EdgeIndex == -1)
			{
				FGroupEdge Edge = { EdgeID };
				Edge.Span = FEdgeSpan(Mesh);
				Edge.Span.InitializeFromEdges(Loop.Edges);
				EdgeIndex = Edges.Add(Edge);
			}
			Boundary.GroupEdges.Add(EdgeIndex);
			continue;
		}

		// duplicate first corner vertex so that we can just loop back around to it w/ modulo count
		int NumSpans = CornerIndices.Num();
		int FirstIdx = CornerIndices[0];
		CornerIndices.Add(FirstIdx);

		// add each span
		for (int k = 0; k < NumSpans; ++k)
		{
			int i0 = CornerIndices[k];

			FIndex2i EdgeID = MakeEdgeID(Loop.Edges[i0]);
			int OtherGroupID = (EdgeID.A == Group.GroupID) ? EdgeID.B : EdgeID.A;
			int EdgeIndex = FindExistingGroupEdge(Group.GroupID, OtherGroupID, Loop.Vertices[i0]);
			if (EdgeIndex != -1)
			{
				FGroupEdge& Existing = Edges[EdgeIndex];
				Boundary.GroupEdges.Add(EdgeIndex);
				continue;
			}

			FGroupEdge Edge = { EdgeID };

			int i1 = CornerIndices[k+1];		// note: i1 == i0 on a closed loop, ie NumSpans == 1
			TArray<int> SpanVertices;
			do {
				SpanVertices.Add(Loop.Vertices[i0]);
				i0 = (i0 + 1) % NumV;
			} while (i0 != i1);
			SpanVertices.Add(Loop.Vertices[i0]);	// add last vertex

			Edge.Span = FEdgeSpan(Mesh);
			Edge.Span.InitializeFromVertices(SpanVertices);
			EdgeIndex = Edges.Add(Edge);
			Boundary.GroupEdges.Add(EdgeIndex);
		}
	}
}




int FGroupTopology::FindExistingGroupEdge(int GroupID, int OtherGroupID, int FirstVertexID)
{
	// if this is a boundary edge, we cannot have created it already
	if (OtherGroupID < 0)
	{
		return -1;
	}

	FGroup& OtherGroup = Groups[GroupIDToGroupIndexMap[OtherGroupID]];
	FIndex2i EdgeID = MakeEdgeID(GroupID, OtherGroupID);
	
	for (FGroupBoundary& Boundary : OtherGroup.Boundaries)
	{
		for (int EdgeIndex : Boundary.GroupEdges)
		{
			if (Edges[EdgeIndex].Groups == EdgeID)
			{
				// same EdgeID pair may occur multiple times in the same boundary loop! 
				// need to check that at least one endpoint is the same vertex

				TArray<int>& Vertices = Edges[EdgeIndex].Span.Vertices;
				if (Vertices[0] == FirstVertexID || Vertices[Vertices.Num() - 1] == FirstVertexID)
				{
					return EdgeIndex;
				}
			}
		}
	}
	return -1;
}



bool FGroupTopology::GetGroupEdgeTangent(int GroupEdgeID, FVector3d& TangentOut) const
{
	check(GroupEdgeID >= 0 && GroupEdgeID < Edges.Num());
	const FGroupEdge& Edge = Edges[GroupEdgeID];
	FVector3d StartPos = Mesh->GetVertex(Edge.Span.Vertices[0]);
	FVector3d EndPos = Mesh->GetVertex(Edge.Span.Vertices[Edge.Span.Vertices.Num()-1]);
	if (StartPos.DistanceSquared(EndPos) > 100 * FMathd::ZeroTolerance)
	{
		TangentOut = (EndPos - StartPos).Normalized();
		return true;
	}
	else
	{
		TangentOut = FVector3d::UnitX();
		return false;
	}
}