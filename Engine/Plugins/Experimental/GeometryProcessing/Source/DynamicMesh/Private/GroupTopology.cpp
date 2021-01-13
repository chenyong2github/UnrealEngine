// Copyright Epic Games, Inc. All Rights Reserved.

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
	this->GroupLayer = nullptr;
	if (bAutoBuild)
	{
		RebuildTopology();
	}
}


FGroupTopology::FGroupTopology(const FDynamicMesh3* MeshIn, const FDynamicMeshPolygroupAttribute* GroupLayerIn, bool bAutoBuild)
{
	this->Mesh = MeshIn;
	this->GroupLayer = GroupLayerIn;
	if (bAutoBuild)
	{
		RebuildTopology();
	}
}


bool FGroupTopology::RebuildTopology()
{
	Groups.Reset();
	Edges.Reset();
	Corners.Reset();

	int32 MaxGroupID = 0;
	for (int32 tid : Mesh->TriangleIndicesItr())
	{
		MaxGroupID = FMath::Max(GetGroupID(tid), MaxGroupID);
	}
	MaxGroupID++;

	// initialize groups map first to avoid resizes
	GroupIDToGroupIndexMap.Reset();
	GroupIDToGroupIndexMap.Init(-1, MaxGroupID);
	TArray<int> GroupFaceCounts;
	GroupFaceCounts.Init(0, MaxGroupID);
	for (int tid : Mesh->TriangleIndicesItr())
	{
		int GroupID = FMathd::Max(0, GetGroupID(tid));
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
		Group.Triangles.Reserve(GroupFaceCounts[Group.GroupID]);
	}


	// sort faces into groups
	for (int tid : Mesh->TriangleIndicesItr())
	{
		int GroupID = FMathd::Max(0, GetGroupID(tid));
		Groups[GroupIDToGroupIndexMap[GroupID]].Triangles.Add(tid);
	}

	// precompute junction vertices set
	CornerVerticesFlags.Init(false, Mesh->MaxVertexID());
	for (int vid : Mesh->VertexIndicesItr())
	{
		if (IsCornerVertex(vid))
		{
			CornerVerticesFlags[vid] = true;
			FCorner Corner = { vid };
			int NewCornerIndex = Corners.Num();
			VertexIDToCornerIDMap.Add(vid, NewCornerIndex);
			Corners.Add(Corner);
		}
	}
	for (FCorner& Corner : Corners)
	{
		GetAllVertexGroups(Corner.VertexID, Corner.NeighbourGroupIDs);
	}


	// construct boundary loops
	for (FGroup& Group : Groups)
	{
		// finds FGroupEdges and uses to populate Group.Boundaries
		bool bOK = ExtractGroupEdges(Group);
		if (!bOK)
		{
			return false;
		}

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

	return true;
}


void FGroupTopology::RetargetOnClonedMesh(const FDynamicMesh3* NewMesh)
{
	Mesh = NewMesh;
	for (FGroupEdge& Edge : Edges)
	{
		Edge.Span.Mesh = NewMesh;
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
	if (UniqueCount == 2 && Mesh->IsBoundaryVertex(VertexID))
	{
		return true;
	}
	return false;
}




int FGroupTopology::GetCornerVertexID(int CornerID) const
{
	check(CornerID >= 0 && CornerID < Corners.Num());
	return Corners[CornerID].VertexID;
}

int32 FGroupTopology::GetCornerIDFromVertexID(int32 VertexID) const
{
	check(Mesh->IsVertex(VertexID));
	const int32* Found = VertexIDToCornerIDMap.Find(VertexID);
	return (Found == nullptr) ? IndexConstants::InvalidID : *Found;
}


const FGroupTopology::FGroup* FGroupTopology::FindGroupByID(int GroupID) const
{
	if (GroupID < 0 || GroupID >= GroupIDToGroupIndexMap.Num() || GroupIDToGroupIndexMap[GroupID] == -1)
	{
		return nullptr;
	}
	return &Groups[GroupIDToGroupIndexMap[GroupID]];
}


const TArray<int>& FGroupTopology::GetGroupTriangles(int GroupID) const
{
	const FGroup* Found = FindGroupByID(GroupID);
	ensure(Found != nullptr);
	return (Found != nullptr) ? Found->Triangles : EmptyArray;
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

const TArray<int>& FGroupTopology::GetGroupEdgeEdges(int GroupEdgeID) const
{
	check(GroupEdgeID >= 0 && GroupEdgeID < Edges.Num());
	return Edges[GroupEdgeID].Span.Edges;
}

bool FGroupTopology::IsSimpleGroupEdge(int32 GroupEdgeID) const
{
	check(GroupEdgeID >= 0 && GroupEdgeID < Edges.Num());
	return Edges[GroupEdgeID].Span.Edges.Num() == 1;
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

bool FGroupTopology::IsBoundaryEdge(int32 GroupEdgeID) const
{
	return Mesh->IsBoundaryEdge(Edges[GroupEdgeID].Span.Edges[0]);
}

bool FGroupTopology::IsIsolatedLoop(int GroupEdgeID) const
{
	const FGroupEdge& Edge = Edges[GroupEdgeID];
	return Edge.EndpointCorners[0] == IndexConstants::InvalidID;
}



double FGroupTopology::GetEdgeArcLength(int32 GroupEdgeID, TArray<double>* PerVertexLengthsOut) const
{
	check(GroupEdgeID >= 0 && GroupEdgeID < Edges.Num());
	const TArray<int32>& Vertices = GetGroupEdgeVertices(GroupEdgeID);
	int32 NumV = Vertices.Num();
	if (PerVertexLengthsOut != nullptr)
	{
		PerVertexLengthsOut->SetNum(NumV);
		(*PerVertexLengthsOut)[0] = 0.0;
	}
	double AccumLength = 0;
	for (int32 k = 1; k < NumV; ++k)
	{
		AccumLength += Mesh->GetVertex(Vertices[k]).Distance(Mesh->GetVertex(Vertices[k-1]));
		if (PerVertexLengthsOut != nullptr)
		{
			(*PerVertexLengthsOut)[k] = AccumLength;
		}
	}
	return AccumLength;
}


FVector3d FGroupTopology::GetEdgeMidpoint(int32 GroupEdgeID, double* ArcLengthOut, TArray<double>* PerVertexLengthsOut) const
{
	check(GroupEdgeID >= 0 && GroupEdgeID < Edges.Num());
	const TArray<int32>& Vertices = GetGroupEdgeVertices(GroupEdgeID);
	int32 NumV = Vertices.Num();

	// trivial case
	if (NumV == 2)
	{
		FVector3d A(Mesh->GetVertex(Vertices[0])), B(Mesh->GetVertex(Vertices[1]));
		if (ArcLengthOut)
		{
			*ArcLengthOut = A.Distance(B);
		}
		if (PerVertexLengthsOut)
		{
			(*PerVertexLengthsOut).SetNum(2);
			(*PerVertexLengthsOut)[0] = 0;
			(*PerVertexLengthsOut)[1] = A.Distance(B);
		}
		return (A + B) * 0.5;
	}

	// if we want lengths anyway we can avoid second loop
	if (PerVertexLengthsOut)
	{
		double Len = GetEdgeArcLength(GroupEdgeID, PerVertexLengthsOut);
		if (ArcLengthOut)
		{
			*ArcLengthOut = Len;
		}
		Len /= 2;
		int32 k = 0;
		while ((*PerVertexLengthsOut)[k] < Len)
		{
			k++;
		}
		int32 kprev = k - 1;
		double a = (*PerVertexLengthsOut)[k-1], b = (*PerVertexLengthsOut)[k];
		double t = (Len - a) / (b - a);
		FVector3d A(Mesh->GetVertex(Vertices[k-1])), B(Mesh->GetVertex(Vertices[k]));
		return FVector3d::Lerp(A, B, t);
	}

	// compute arclen and then walk forward until we get halfway
	double Len = GetEdgeArcLength(GroupEdgeID);
	if (ArcLengthOut)
	{
		*ArcLengthOut = Len;
	}
	Len /= 2;
	double AccumLength = 0;
	for (int32 k = 1; k < NumV; ++k)
	{
		double NewLen = AccumLength + Mesh->GetVertex(Vertices[k]).Distance(Mesh->GetVertex(Vertices[k-1]));
		if ( NewLen > Len )
		{
			double t = (Len - AccumLength) / (NewLen - AccumLength);
			FVector3d A(Mesh->GetVertex(Vertices[k - 1])), B(Mesh->GetVertex(Vertices[k]));
			return FVector3d::Lerp(A, B, t);
		}
		AccumLength = NewLen;
	}

	// somehow failed?
	return (Mesh->GetVertex(Vertices[0]) + Mesh->GetVertex(Vertices[NumV-1])) * 0.5;
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
		for (int TriID : Found->Triangles)
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



bool FGroupTopology::ExtractGroupEdges(FGroup& Group)
{
	FMeshRegionBoundaryLoops BdryLoops(Mesh, Group.Triangles, true);

	if (BdryLoops.bFailed)
	{
		// Unrecoverable error when trying to find the group boundary loops 
		return false;
	}

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
			int EdgeIndex = FindExistingGroupEdge(Group.GroupID, OtherGroupID, Loop.Vertices[0], Loop.Vertices[1]);
			if (EdgeIndex == -1)
			{
				FGroupEdge Edge = { EdgeID };
				Edge.Span = FEdgeSpan(Mesh);
				Edge.Span.InitializeFromEdges(Loop.Edges);
				Edge.EndpointCorners = FIndex2i::Invalid();
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
			int EdgeIndex = FindExistingGroupEdge(Group.GroupID, OtherGroupID, Loop.Vertices[i0], Loop.Vertices[(i0+1)%Loop.Vertices.Num()]);
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
			Edge.EndpointCorners = FIndex2i(GetCornerIDFromVertexID(SpanVertices[0]), GetCornerIDFromVertexID(SpanVertices.Last()));
			check(Edge.EndpointCorners.A != IndexConstants::InvalidID && Edge.EndpointCorners.B != IndexConstants::InvalidID);
			EdgeIndex = Edges.Add(Edge);
			Boundary.GroupEdges.Add(EdgeIndex);
		}
	}

	return true;
}




int FGroupTopology::FindExistingGroupEdge(int GroupID, int OtherGroupID, int FirstVertexID, int SecondVertexID)
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
				// Same EdgeID pair may occur multiple times in the same boundary loop
				// (think of a cube with its side faces joined together on opposite corners).
				// For non-loop edges, it is sufficient to check that one of the endpoints is the
				// same vertex to know that the edges are the same.
				TArray<int>& Vertices = Edges[EdgeIndex].Span.Vertices;
				int32 NumVerts = Vertices.Num();
				if (Edges[EdgeIndex].EndpointCorners.A != IndexConstants::InvalidID)
				{
					if (Vertices[0] == FirstVertexID || Vertices[NumVerts - 1] == FirstVertexID)
					{
						return EdgeIndex;
					}
				}
				else
				{
					// For loop edges we're not guaranteed to have the loop start on any particular
					// vertex. We have to make sure that the two loops share at least two adjacent 
					// vertices, because of pathological cases with bowtie-shaped groups.
					int32 FirstVertIndex = Vertices.IndexOfByKey(FirstVertexID);
					if (FirstVertIndex != INDEX_NONE 
						&& (Vertices[(FirstVertIndex + 1) % NumVerts] == SecondVertexID
							|| Vertices[(FirstVertIndex + NumVerts - 1) % NumVerts] == SecondVertexID))
					{
						return EdgeIndex;
					}
				}
			}//end if group pair matched
		}
	}//end looking through other group boundaries
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



FFrame3d FGroupTopology::GetGroupFrame(int32 GroupID) const
{
	FVector3d Centroid = FVector3d::Zero();
	FVector3d Normal = FVector3d::Zero();
	const FGroup& Face = Groups[GroupIDToGroupIndexMap[GroupID]];
	for (int32 tid : Face.Triangles)
	{
		Centroid += Mesh->GetTriCentroid(tid);
		Normal += Mesh->GetTriNormal(tid);
	}
	Centroid /= (double)Face.Triangles.Num();
	Normal.Normalize();
	return FFrame3d(Centroid, Normal);
}


FFrame3d FGroupTopology::GetSelectionFrame(const FGroupTopologySelection& Selection, FFrame3d* InitialLocalFrame) const
{
	int32 NumCorners = Selection.SelectedCornerIDs.Num();
	int32 NumEdges = Selection.SelectedEdgeIDs.Num();
	int32 NumFaces = Selection.SelectedGroupIDs.Num();

	FFrame3d StartFrame = (InitialLocalFrame) ? (*InitialLocalFrame) : FFrame3d();
	if (NumEdges == 1)
	{
		int32 EdgeID = Selection.GetASelectedEdgeID();
		FVector3d Tangent;
		if (GetGroupEdgeTangent(EdgeID, Tangent))
		{
			StartFrame.ConstrainedAlignAxis(0, Tangent, StartFrame.Z());
		}
		StartFrame.Origin = GetEdgeMidpoint(EdgeID);
		return StartFrame;
	}
	if (NumCorners == 1)
	{
		StartFrame.Origin = Mesh->GetVertex(Corners[Selection.GetASelectedCornerID()].VertexID);
		return StartFrame;
	}

	FVector3d AccumulatedOrigin = FVector3d::Zero();
	FVector3d AccumulatedNormal = FVector3d::Zero();

	int AccumCount = 0;
	for (int32 CornerID : Selection.SelectedCornerIDs)
	{
		AccumulatedOrigin += Mesh->GetVertex(GetCornerVertexID(CornerID));
		AccumulatedNormal += FVector3d::UnitZ();
		AccumCount++;
	}

	for (int32 EdgeID : Selection.SelectedEdgeIDs)
	{
		const FGroupEdge& Edge = Edges[EdgeID];
		FVector3d StartPos = Mesh->GetVertex(Edge.Span.Vertices[0]);
		FVector3d EndPos = Mesh->GetVertex(Edge.Span.Vertices[Edge.Span.Vertices.Num() - 1]);
		AccumulatedOrigin +=  0.5*(StartPos + EndPos);
		AccumulatedNormal += FVector3d::UnitZ();
		AccumCount++;
	}

	for (int32 GroupID : Selection.SelectedGroupIDs)
	{
		if (FindGroupByID(GroupID) != nullptr)
		{
			FFrame3d GroupFrame = GetGroupFrame(GroupID);
			AccumulatedOrigin += GroupFrame.Origin;
			AccumulatedNormal += GroupFrame.Z();
			AccumCount++;
		}
	}

	FFrame3d AccumulatedFrame;
	if (AccumCount > 0)
	{
		AccumulatedOrigin /= (double)AccumCount;
		AccumulatedNormal.Normalize();

		// We set our frame Z to be accumulated normal, and the other two axes are unconstrained, so
		// we want to set them to something that will make our frame generally more useful. If the normal
		// is aligned with world Z, then the entire frame might as well be aligned with world.
		if (1 - AccumulatedNormal.Dot(FVector3d::UnitZ()) < KINDA_SMALL_NUMBER)
		{
			AccumulatedFrame = FFrame3d(AccumulatedOrigin, FQuaterniond::Identity());
		}
		else
		{
			// Otherwise, let's place one of the other axes into the XY plane so that the frame is more
			// useful for translation. We somewhat arbitrarily choose Y for this. 
			FVector3d FrameY = AccumulatedNormal.Cross(FVector3d::UnitZ()).Normalized(); // orthogonal to world Z and frame Z 
			FVector3d FrameX = FrameY.Cross(AccumulatedNormal); // safe to not normalize because already orthogonal
			AccumulatedFrame = FFrame3d(AccumulatedOrigin, FrameX, FrameY, AccumulatedNormal);
		}
	}

	return AccumulatedFrame;
}





void FGroupTopology::GetSelectedTriangles(const FGroupTopologySelection& Selection, TArray<int32>& Triangles) const
{
	for (int32 GroupID : Selection.SelectedGroupIDs)
	{
		for (int32 TriangleID : GetGroupTriangles(GroupID))
		{
			Triangles.Add(TriangleID);
		}
	}
}



void FGroupTopology::GetAllVertexGroups(int32 VertexID, TArray<int32>& GroupsOut) const
{
	for (int32 EdgeID : Mesh->VtxEdgesItr(VertexID))
	{
		FIndex2i EdgeTris = Mesh->GetEdgeT(EdgeID);
		GroupsOut.AddUnique(GetGroupID(EdgeTris.A));
		if (EdgeTris.B != FDynamicMesh3::InvalidID)
		{
			GroupsOut.AddUnique(GetGroupID(EdgeTris.B));
		}
	}
}






FTriangleGroupTopology::FTriangleGroupTopology(const FDynamicMesh3* Mesh, bool bAutoBuild) 
	: FGroupTopology(Mesh, bAutoBuild)
{
}


bool FTriangleGroupTopology::RebuildTopology()
{
	Groups.Reset();
	Edges.Reset();
	Corners.Reset();

	int32 MaxGroupID = Mesh->MaxTriangleID();

	// initialize groups
	GroupIDToGroupIndexMap.Reset();
	GroupIDToGroupIndexMap.Init(-1, MaxGroupID);
	TArray<int> GroupFaceCounts;
	GroupFaceCounts.Init(1, MaxGroupID);
	for (int tid : Mesh->TriangleIndicesItr())
	{
		if (GroupIDToGroupIndexMap[tid] == -1)
		{
			FGroup NewGroup;
			NewGroup.GroupID = tid;
			NewGroup.Triangles.Add(tid);
			GroupIDToGroupIndexMap[tid] = Groups.Add(NewGroup);
		}
	}

	CornerVerticesFlags.Init(true, Mesh->MaxVertexID());
	for (int vid : Mesh->VertexIndicesItr())
	{
		CornerVerticesFlags[vid] = true;
		FCorner Corner = { vid };
		int NewCornerIndex = Corners.Num();
		VertexIDToCornerIDMap.Add(vid, NewCornerIndex);
		Corners.Add(Corner);
	}
	for (FCorner& Corner : Corners)
	{
		GetAllVertexGroups(Corner.VertexID, Corner.NeighbourGroupIDs);
	}

	TArray<int32> MeshEdgeToGroupEdge;
	MeshEdgeToGroupEdge.Init(-1, Mesh->MaxEdgeID());

	// construct boundary loops
	TArray<int> SpanVertices; SpanVertices.SetNum(2);
	for (FGroup& Group : Groups)
	{
		// find FGroupEdges and uses to populate Group.Boundaries
		Group.Boundaries.SetNum(1);
		FGroupBoundary& Boundary0 = Group.Boundaries[0];

		FIndex3i TriEdges = Mesh->GetTriEdges(Group.GroupID);
		for (int j = 0; j < 3; ++j)
		{
			if (MeshEdgeToGroupEdge[TriEdges[j]] != -1)
			{
				Boundary0.GroupEdges.Add(TriEdges[j]);
			}
			else
			{
				FGroupEdge NewGroupEdge = { MakeEdgeID(TriEdges[j]) };
				FIndex2i EdgeVerts = Mesh->GetEdgeV(TriEdges[j]);
				NewGroupEdge.Span = FEdgeSpan(Mesh);
				SpanVertices[0] = EdgeVerts.A; SpanVertices[1] = EdgeVerts.B;
				NewGroupEdge.Span.InitializeFromVertices(SpanVertices);
				NewGroupEdge.EndpointCorners = FIndex2i(GetCornerIDFromVertexID(SpanVertices[0]), GetCornerIDFromVertexID(SpanVertices[1]));
				check(NewGroupEdge.EndpointCorners.A != IndexConstants::InvalidID && NewGroupEdge.EndpointCorners.B != IndexConstants::InvalidID);
				int32 EdgeIndex = Edges.Add(NewGroupEdge);
				Boundary0.GroupEdges.Add(EdgeIndex);
				MeshEdgeToGroupEdge[TriEdges[j]] = EdgeIndex;
			}
		}

		// collect up .NeighbourGroupIDs and set .bIsOnBoundary
		// make all-neighbour-groups list at group level
		Boundary0.bIsOnBoundary = false;
		FIndex3i TriNbrTris = Mesh->GetTriNeighbourTris(Group.GroupID);
		for (int j = 0; j < 3; ++j)
		{
			if (TriNbrTris[j] != FDynamicMesh3::InvalidID)
			{
				Group.NeighbourGroupIDs.Add(TriNbrTris[j]);
				Boundary0.NeighbourGroupIDs.Add(TriNbrTris[j]);
			}
			else
			{
				Boundary0.bIsOnBoundary = true;
			}
		}
	}

	return true;
}
