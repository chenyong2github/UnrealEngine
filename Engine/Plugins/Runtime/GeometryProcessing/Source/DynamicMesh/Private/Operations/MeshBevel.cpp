// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/MeshBevel.h"

#include "GroupTopology.h"
#include "EdgeLoop.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "MeshWeights.h"
#include "CompGeom/PolygonTriangulation.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshIndexUtil.h"
#include "Algo/Count.h"

using namespace UE::Geometry;



void FMeshBevel::InitializeFromGroupTopology(const FDynamicMesh3& Mesh, const FGroupTopology& Topology)
{
	ResultInfo = FGeometryResult(EGeometryResultType::InProgress);

	// set up initial problem inputs
	for (int32 TopoEdgeID = 0; TopoEdgeID < Topology.Edges.Num(); ++TopoEdgeID)
	{
		if (Topology.IsIsolatedLoop(TopoEdgeID))
		{
			FEdgeLoop NewLoop;
			NewLoop.InitializeFromEdges(&Mesh, Topology.Edges[TopoEdgeID].Span.Edges);
			AddBevelEdgeLoop(Mesh, NewLoop);
		}
		else
		{
			AddBevelGroupEdge(Mesh, Topology, TopoEdgeID);
		}

		if (ResultInfo.CheckAndSetCancelled(Progress))
		{
			return;
		}
	}

	// precompute topological information necessary to apply bevel to vertices/edges/loops
	BuildVertexSets(Mesh);
}


void FMeshBevel::InitializeFromGroupTopology(const FDynamicMesh3& Mesh, const FGroupTopology& Topology, const TArray<int32>& GroupEdges)
{
	ResultInfo = FGeometryResult(EGeometryResultType::InProgress);

	// set up initial problem inputs
	for (int32 TopoEdgeID : GroupEdges)
	{
		if (Topology.IsIsolatedLoop(TopoEdgeID))
		{
			FEdgeLoop NewLoop;
			NewLoop.InitializeFromEdges(&Mesh, Topology.Edges[TopoEdgeID].Span.Edges);
			AddBevelEdgeLoop(Mesh, NewLoop);
		}
		else
		{
			AddBevelGroupEdge(Mesh, Topology, TopoEdgeID);
		}

		if (ResultInfo.CheckAndSetCancelled(Progress))
		{
			return;
		}
	}

	// precompute topological information necessary to apply bevel to vertices/edges/loops
	BuildVertexSets(Mesh);
}


bool FMeshBevel::Apply(FDynamicMesh3& Mesh, FDynamicMeshChangeTracker* ChangeTracker)
{
	// disconnect along bevel graph edges/vertices and save necessary info
	UnlinkEdges(Mesh, ChangeTracker);
	if (ResultInfo.CheckAndSetCancelled(Progress))
	{
		return false;
	}
	UnlinkLoops(Mesh, ChangeTracker);
	if (ResultInfo.CheckAndSetCancelled(Progress))
	{
		return false;
	}
	UnlinkVertices(Mesh, ChangeTracker);
	if (ResultInfo.CheckAndSetCancelled(Progress))
	{
		return false;
	}

	// update vertex positions
	DisplaceVertices(Mesh, InsetDistance);
	if (ResultInfo.CheckAndSetCancelled(Progress))
	{
		return false;
	}

	// mesh the bevel corners and edges
	CreateBevelMeshing(Mesh);
	if (ResultInfo.CheckAndSetCancelled(Progress))
	{
		return false;
	}

	// compute normals
	ComputeNormals(Mesh);
	if (ResultInfo.CheckAndSetCancelled(Progress))
	{
		return false;
	}

	// todo: compute UVs, other attribs

	ResultInfo.SetSuccess(true, Progress);
	return true;
}



FMeshBevel::FBevelVertex* FMeshBevel::GetBevelVertexFromVertexID(int32 VertexID)
{
	int32* FoundIndex = VertexIDToIndexMap.Find(VertexID);
	if (FoundIndex == nullptr)
	{
		return nullptr;
	}
	return &Vertices[*FoundIndex];
}



void FMeshBevel::AddBevelGroupEdge(const FDynamicMesh3& Mesh, const FGroupTopology& Topology, int32 GroupEdgeID)
{
	const TArray<int32>& MeshEdgeList = Topology.Edges[GroupEdgeID].Span.Edges;

	// currently cannot handle boundary edges
	if ( Algo::CountIf(MeshEdgeList, [&Mesh](int EdgeID) { return Mesh.IsBoundaryEdge(EdgeID); }) > 0 )
	{
		return;
	}

	FIndex2i EdgeCornerIDs = Topology.Edges[GroupEdgeID].EndpointCorners;

	FBevelEdge Edge;

	for (int32 ci = 0; ci < 2; ++ci)
	{
		int32 CornerID = EdgeCornerIDs[ci];
		FGroupTopology::FCorner Corner = Topology.Corners[CornerID];
		int32 VertexID = Corner.VertexID;
		Edge.bEndpointBoundaryFlag[ci] = Mesh.IsBoundaryVertex(VertexID);
		int32 IncomingEdgeID = (ci == 0) ? MeshEdgeList[0] : MeshEdgeList.Last();

		FBevelVertex* VertInfo = GetBevelVertexFromVertexID(VertexID);
		if (VertInfo == nullptr)
		{
			FBevelVertex NewVertex;
			NewVertex.CornerID = CornerID;
			NewVertex.VertexID = VertexID;
			int32 NewIndex = Vertices.Num();
			Vertices.Add(NewVertex);
			VertexIDToIndexMap.Add(VertexID, NewIndex);
			VertInfo = &Vertices[NewIndex];
		}
		VertInfo->IncomingBevelMeshEdges.Add(IncomingEdgeID);
		VertInfo->IncomingBevelTopoEdges.Add(GroupEdgeID);
	}

	Edge.MeshEdges.Append(MeshEdgeList);
	Edge.MeshVertices.Append(Topology.Edges[GroupEdgeID].Span.Vertices);
	Edge.GroupEdgeID = GroupEdgeID;
	Edge.GroupIDs = Topology.Edges[GroupEdgeID].Groups;
	Edges.Add(MoveTemp(Edge));
}





void FMeshBevel::AddBevelEdgeLoop(const FDynamicMesh3& Mesh, const FEdgeLoop& MeshEdgeLoop)
{
	// currently cannot handle boundary edges
	if ( Algo::CountIf(MeshEdgeLoop.Edges, [&Mesh](int EdgeID) { return Mesh.IsBoundaryEdge(EdgeID); }) > 0 )
	{
		return;
	}

	FBevelLoop Loop;
	Loop.MeshEdges = MeshEdgeLoop.Edges;
	Loop.MeshVertices = MeshEdgeLoop.Vertices;

	Loops.Add(Loop);
}




void FMeshBevel::BuildVertexSets(const FDynamicMesh3& Mesh)
{
	// can be parallel
	for (FBevelVertex& Vertex : Vertices)
	{
		// get sorted list of triangles around the vertex
		TArray<int> GroupLengths;
		TArray<bool> bGroupIsLoop;
		EMeshResult Result = Mesh.GetVtxContiguousTriangles(Vertex.VertexID, Vertex.SortedTriangles, GroupLengths, bGroupIsLoop);
		if ( Result != EMeshResult::Ok || GroupLengths.Num() != 1 || Vertex.SortedTriangles.Num() < 2)
		{
			Vertex.VertexType = EBevelVertexType::Unknown;
			continue;
		}

		// GetVtxContiguousTriangles does not return triangles sorted in a consistent direction. This check will
		// reverse the ordering such that it is consistently walking counter-clockwise around the vertex (I think...)
		FIndex3i Tri0 = Mesh.GetTriangle(Vertex.SortedTriangles[0]).GetCycled(Vertex.VertexID);
		FIndex3i Tri1 = Mesh.GetTriangle(Vertex.SortedTriangles[1]).GetCycled(Vertex.VertexID);
		if (Tri0.C == Tri1.B)
		{
			Algo::Reverse(Vertex.SortedTriangles);
		}

		if (Mesh.IsBoundaryVertex(Vertex.VertexID))
		{
			Vertex.VertexType = EBevelVertexType::BoundaryVertex;
			continue;
		}


		checkSlow(Vertex.IncomingBevelMeshEdges.Num() != 0);		// shouldn't ever happen
		if (Vertex.IncomingBevelMeshEdges.Num() == 1)
		{
			BuildTerminatorVertex(Vertex, Mesh);
		}
		else
		{
			BuildJunctionVertex(Vertex, Mesh);
		}

		if (ResultInfo.CheckAndSetCancelled(Progress))
		{
			return;
		}
	}
}



void FMeshBevel::BuildJunctionVertex(FBevelVertex& Vertex, const FDynamicMesh3& Mesh)
{
	//
	// Now split up the single contiguous one-ring into "Wedges" created by the incoming split-edges
	//

	// find first split edge, and the triangle/index "after" that first split edge
	int32 NT = Vertex.SortedTriangles.Num();
	int32 StartTriIndex = -1;
	for (int32 k = 0; k < NT; ++k)
	{
		if (FindSharedEdgeInTriangles(Mesh, Vertex.SortedTriangles[k], Vertex.SortedTriangles[(k + 1) % NT]) == Vertex.IncomingBevelMeshEdges[0])
		{
			StartTriIndex = (k + 1) % NT;		// start at second tri, so that bevel-edge is first edge in wedge
			break;
		}
	}
	if (StartTriIndex == -1)
	{
		Vertex.VertexType = EBevelVertexType::Unknown;
		return;
	}

	// now walk around the one-ring tris, accumulating into current Wedge until we hit another split-edge,
	// at which point a new Wedge is spawned
	int32 CurTriIndex = StartTriIndex;
	FOneRingWedge CurWedge;
	CurWedge.WedgeVertex = Vertex.VertexID;
	CurWedge.Triangles.Add(Vertex.SortedTriangles[CurTriIndex]);
	CurWedge.BorderEdges.A = Vertex.IncomingBevelMeshEdges[0];
	for (int32 k = 0; k < NT; ++k)
	{
		int32 CurTri = Vertex.SortedTriangles[CurTriIndex % NT];
		int32 NextTri = Vertex.SortedTriangles[(CurTriIndex + 1) % NT];
		int32 SharedEdge = FindSharedEdgeInTriangles(Mesh, CurTri, NextTri);
		checkSlow(SharedEdge != -1);
		if (Vertex.IncomingBevelMeshEdges.Contains(SharedEdge))
		{
			// if we found a bevel-edge, close the current wedge and start a new one
			CurWedge.BorderEdges.B = SharedEdge;
			Vertex.Wedges.Add(CurWedge);
			CurWedge = FOneRingWedge();
			CurWedge.WedgeVertex = Vertex.VertexID;
			CurWedge.BorderEdges.A = SharedEdge;
		}
		CurWedge.Triangles.Add(NextTri);
		CurTriIndex++;
	}
	// ?? is there a chance that we have a final open wedge here? we iterate one extra time so it shouldn't happen (or could we get an extra wedge then??)

	for (FOneRingWedge& Wedge : Vertex.Wedges)
	{
		Wedge.BorderEdgeTriEdgeIndices.A = Mesh.GetTriEdges(Wedge.Triangles[0]).IndexOf(Wedge.BorderEdges.A);
		Wedge.BorderEdgeTriEdgeIndices.B = Mesh.GetTriEdges(Wedge.Triangles.Last()).IndexOf(Wedge.BorderEdges.B);
	}

	if (Vertex.Wedges.Num() > 1)
	{
		Vertex.VertexType = EBevelVertexType::JunctionVertex;
	}
	else
	{
		Vertex.VertexType = EBevelVertexType::Unknown;
	}
}


void FMeshBevel::BuildTerminatorVertex(FBevelVertex& Vertex, const FDynamicMesh3& Mesh)
{
	Vertex.VertexType = EBevelVertexType::Unknown;
	if (ensure(Vertex.IncomingBevelMeshEdges.Num() == 1) == false)
	{
		return;
	}

	int32 IncomingEdgeID = Vertex.IncomingBevelMeshEdges[0];
	int32 NumTris = Vertex.SortedTriangles.Num();

	// We have one edge coming into the vertex one ring, our main problem is to pick a second
	// edge to split the one-ring with. There is no obvious right answer in many cases. 

	// "other" split edge that we pick
	int32 RingSplitEdgeID = -1;

	// NOTE: code below assumes we have polygroups on the mesh. In theory we could also support not having polygroups,
	// by (eg) picking an arbitrary edge "furthest" from IncomingEdgeID in the exponential map

	// Find the ordered set of triangles that are not in either of the groups connected to IncomingEdgeID. Eg imagine
	// a cube corner, if we want to bevel IncomingEdgeID along one of the cube edges, we want to add the new edge
	// in the "furthest" face (perpendicular to the edge)
	FIndex2i IncomingEdgeT = Mesh.GetEdgeT(IncomingEdgeID);
	FIndex2i IncomingEdgeGroups(Mesh.GetTriangleGroup(IncomingEdgeT.A), Mesh.GetTriangleGroup(IncomingEdgeT.B));
	TArray<int32> VertexGroups;
	Mesh.GetAllVertexGroups(Vertex.VertexID, VertexGroups);
	TArray<int32> OtherGroupTris;	// sorted wedge of triangles that are not in either of the groups connected to incoming edge
	TArray<int32> OtherGroups;		// list of group IDs encountered, in-order
	for (int32 tid : Vertex.SortedTriangles)
	{
		int32 gid = Mesh.GetTriangleGroup(tid);
		if (IncomingEdgeGroups.Contains(gid) == false)
		{
			OtherGroupTris.Add(tid);
			OtherGroups.AddUnique(gid);
		}
	}

	// Determine which edge to split at in the "other" group triangles. If we only have one group
	// then we can try to pick the "middlest" edge. The worst case is when there is only one triangle,
	// then we are picking a not-very-good edge no matter what (potentially we should do an edge split or
	// face poke in that situation). If we have multiple groups then we probably want to pick one
	// of the group-boundary edges inside the triangle-span, ideally the "middlest" but currently we
	// are just picking one arbitrarily...
	if (OtherGroups.Num() == 1)
	{
		Vertex.NewGroupID = OtherGroups[0];
		if (OtherGroupTris.Num() == 1)
		{
			FIndex3i TriEdges = Mesh.GetTriEdges(OtherGroupTris[0]);
			for (int32 j = 0; j < 3; ++j)
			{
				if (Mesh.GetEdgeV(TriEdges[j]).Contains(Vertex.VertexID))
				{
					RingSplitEdgeID = TriEdges[j];
					break;
				}
			}
		}
		else if (OtherGroupTris.Num() == 2)
		{
			RingSplitEdgeID = FindSharedEdgeInTriangles(Mesh, OtherGroupTris[0], OtherGroupTris[1]);
		}
		else
		{
			// TODO: should compute opening angles here and pick the edge closest to the middle of the angular span!!
			int32 j = OtherGroupTris.Num() / 2;
			RingSplitEdgeID = FindSharedEdgeInTriangles(Mesh, OtherGroupTris[j], OtherGroupTris[j+1]);
		}
	}
	else
	{
		for (int32 k = 1; k < OtherGroupTris.Num(); ++k)
		{
			if (Mesh.GetTriangleGroup(OtherGroupTris[k-1]) != Mesh.GetTriangleGroup(OtherGroupTris[k]))
			{
				RingSplitEdgeID = FindSharedEdgeInTriangles(Mesh, OtherGroupTris[k-1], OtherGroupTris[k]);
				Vertex.NewGroupID = Mesh.GetTriangleGroup(OtherGroupTris[k-1]);		// this seems very arbitrary
				break;
			}
		}
	}

	if (RingSplitEdgeID == -1)
	{
		return;
	}

	FIndex2i SplitEdgeV = Mesh.GetEdgeV(RingSplitEdgeID);
	Vertex.TerminatorInfo = FIndex2i(RingSplitEdgeID, SplitEdgeV.OtherElement(Vertex.VertexID));

	TArray<int32> SplitTriSets[2];
	if (SplitInteriorVertexTrianglesIntoSubsets(&Mesh, Vertex.VertexID, IncomingEdgeID, RingSplitEdgeID, SplitTriSets[0], SplitTriSets[1]) == false)
	{
		return;
	}

	Vertex.Wedges.SetNum(2);
	Vertex.Wedges[0].WedgeVertex = Vertex.VertexID;
	Vertex.Wedges[0].Triangles.Append(SplitTriSets[0]);
	Vertex.Wedges[1].WedgeVertex = Vertex.VertexID;
	Vertex.Wedges[1].Triangles.Append(SplitTriSets[1]);

	Vertex.VertexType = EBevelVertexType::TerminatorVertex;
}


void FMeshBevel::UnlinkEdges(FDynamicMesh3& Mesh, FDynamicMeshChangeTracker* ChangeTracker)
{
	for (FBevelEdge& Edge : Edges)
	{
		UnlinkBevelEdgeInterior(Mesh, Edge, ChangeTracker);
	}
}



namespace UELocal
{
	// decomposition of a vertex one-ring into two connected triangle subsets
	struct FVertexSplit
	{
		int32 VertexID;
		bool bOK;
		TArray<int32> TriSets[2];
	};

	// walk along a sequence of vertex-splits and make sure that the split triangle sets
	// maintain consistent "sides" (see call in UnlinkBevelEdgeInterior for more details)
	static void ReconcileTriangleSets(TArray<FVertexSplit>& SplitSequence)
	{
		int32 N = SplitSequence.Num();
		TArray<int32> PrevTriSet0;
		for (int32 k = 0; k < N; ++k)
		{
			if (PrevTriSet0.Num() == 0 && SplitSequence[k].TriSets[0].Num() > 0)
			{
				PrevTriSet0 = SplitSequence[k].TriSets[0];
			}
			else
			{
				bool bFoundInSet0 = false;
				for (int32 tid : SplitSequence[k].TriSets[0])
				{
					if (PrevTriSet0.Contains(tid))
					{
						bFoundInSet0 = true;
						break;
					}
				}
				if (!bFoundInSet0)
				{
					Swap(SplitSequence[k].TriSets[0], SplitSequence[k].TriSets[1]);
				}
				PrevTriSet0 = SplitSequence[k].TriSets[0];
			}
		}
	}

};


void FMeshBevel::UnlinkBevelEdgeInterior(
	FDynamicMesh3& Mesh,
	FBevelEdge& BevelEdge,
	FDynamicMeshChangeTracker* ChangeTracker)
{
	// figure out what sets of triangles to split each vertex into
	int32 N = BevelEdge.MeshVertices.Num();

	TArray<UELocal::FVertexSplit> SplitsToProcess;
	SplitsToProcess.SetNum(N);

	// precompute triangle sets for each vertex we want to split, by "cutting" the one ring into two halves based
	// on edges - 2 edges for interior vertices, and 1 edge for a boundary vertex at the start/end of the edge-span

	SplitsToProcess[0] = UELocal::FVertexSplit{ BevelEdge.MeshVertices[0], false };
	if (BevelEdge.bEndpointBoundaryFlag[0])
	{
		SplitsToProcess[0].bOK = SplitBoundaryVertexTrianglesIntoSubsets(&Mesh, SplitsToProcess[0].VertexID, BevelEdge.MeshEdges[0], 
			SplitsToProcess[0].TriSets[0], SplitsToProcess[0].TriSets[1]);
	}
	for (int32 k = 1; k < N - 1; ++k)
	{
		SplitsToProcess[k].VertexID = BevelEdge.MeshVertices[k];
		if (Mesh.IsBoundaryVertex(SplitsToProcess[k].VertexID))
		{
			SplitsToProcess[k].bOK = false;
		}
		else
		{
			SplitsToProcess[k].bOK = SplitInteriorVertexTrianglesIntoSubsets(&Mesh, SplitsToProcess[k].VertexID,
				BevelEdge.MeshEdges[k-1], BevelEdge.MeshEdges[k], SplitsToProcess[k].TriSets[0], SplitsToProcess[k].TriSets[1]);
		}
	}
	SplitsToProcess[N-1] = UELocal::FVertexSplit{ BevelEdge.MeshVertices[N - 1], false };
	if (BevelEdge.bEndpointBoundaryFlag[1])
	{
		SplitsToProcess[N-1].bOK = SplitBoundaryVertexTrianglesIntoSubsets(&Mesh, SplitsToProcess[N-1].VertexID, BevelEdge.MeshEdges[N-2], 
			SplitsToProcess[N-1].TriSets[0], SplitsToProcess[N-1].TriSets[1]);
	}

	// SplitInteriorVertexTrianglesIntoSubsets does not consistently order its output sets, ie, if you imagine [Edge0,Edge1] as a path
	// cutting through the one ring, the "side" that Set0 and Set1 end up is arbitrary, and depends on the ordering of edges in the triangles of Edge1.
	// This might ideally be fixed in the future, but for the time being, all we need is consistency. So we walk from the start of the 
	// edge to the end, checking for overlap between each tri-one-ring-wedge. If Split[k].TriSet0 does not overlap with Split[k-1].TriSet0, then
	// we want to swap TriSet0 and TriSet1 at Split[k].
	UELocal::ReconcileTriangleSets(SplitsToProcess);

	// apply vertex splits and accumulate new list
	N = SplitsToProcess.Num();
	for (int32 k = 0; k < N; ++k)
	{
		const UELocal::FVertexSplit& Split = SplitsToProcess[k];
		if (ChangeTracker)
		{
			ChangeTracker->SaveVertexOneRingTriangles(Split.VertexID, true);
		}

		bool bDone = false;
		if (Split.bOK)
		{
			FDynamicMesh3::FVertexSplitInfo SplitInfo;
			EMeshResult Result = Mesh.SplitVertex(Split.VertexID, Split.TriSets[0], SplitInfo);
			if (ensure(Result == EMeshResult::Ok))
			{
				BevelEdge.NewMeshVertices.Add(SplitInfo.NewVertex);
				bDone = true;
			}
		}
		if (!bDone)
		{
			BevelEdge.NewMeshVertices.Add(Split.VertexID);
		}
	}

	// now build edge correspondences
	N = BevelEdge.MeshVertices.Num();
	checkSlow(N == BevelEdge.NewMeshVertices.Num());
	for (int32 k = 0; k < N-1; ++k)
	{
		int32 Edge0 = BevelEdge.MeshEdges[k];
		int32 Edge1 = Mesh.FindEdge(BevelEdge.NewMeshVertices[k], BevelEdge.NewMeshVertices[k + 1]);
		BevelEdge.NewMeshEdges.Add(Edge1);
		checkSlow(Edge1 >= 0);
		if ( Mesh.IsEdge(Edge1) && Edge0 != Edge1 && MeshEdgePairs.Contains(Edge0) == false )
		{
			MeshEdgePairs.Add(Edge0, Edge1);
			MeshEdgePairs.Add(Edge1, Edge0);
		}
	}
}




void FMeshBevel::UnlinkBevelLoop(FDynamicMesh3& Mesh, FBevelLoop& BevelLoop, FDynamicMeshChangeTracker* ChangeTracker)
{
	int32 N = BevelLoop.MeshVertices.Num();

	TArray<UELocal::FVertexSplit> SplitsToProcess;
	SplitsToProcess.SetNum(N);

	// precompute triangle sets for each vertex we want to split
	for (int32 k = 0; k < N; ++k)
	{
		SplitsToProcess[k].VertexID = BevelLoop.MeshVertices[k];
		if (Mesh.IsBoundaryVertex(SplitsToProcess[k].VertexID))
		{
			// cannot split boundary vertex
			SplitsToProcess[k].bOK = false;
		}
		else
		{
			int32 PrevEdge = (k == 0) ? BevelLoop.MeshEdges.Last() : BevelLoop.MeshEdges[k-1];
			int32 CurEdge = BevelLoop.MeshEdges[k];
			SplitsToProcess[k].bOK = SplitInteriorVertexTrianglesIntoSubsets(&Mesh, SplitsToProcess[k].VertexID,
				PrevEdge, CurEdge, SplitsToProcess[k].TriSets[0], SplitsToProcess[k].TriSets[1]);
		}
	}

	// fix up triangle sets - see call in UnlinkBevelEdgeInterior() for more info
	UELocal::ReconcileTriangleSets(SplitsToProcess);

	// apply vertex splits and accumulate new list
	N = SplitsToProcess.Num();
	for (int32 k = 0; k < N; ++k)
	{
		const UELocal::FVertexSplit& Split = SplitsToProcess[k];
		if (ChangeTracker)
		{
			ChangeTracker->SaveVertexOneRingTriangles(Split.VertexID, true);
		}

		bool bDone = false;
		if (Split.bOK)
		{
			FDynamicMesh3::FVertexSplitInfo SplitInfo;
			EMeshResult Result = Mesh.SplitVertex(Split.VertexID, Split.TriSets[1], SplitInfo);
			if (Result == EMeshResult::Ok)
			{
				BevelLoop.NewMeshVertices.Add(SplitInfo.NewVertex);
				bDone = true;
			}
		}
		if (!bDone)
		{
			BevelLoop.NewMeshVertices.Add(Split.VertexID);		// failed to split, so we have a shared vertex on both "sides"
		}
	}

	// now build edge correspondences
	N = BevelLoop.MeshVertices.Num();
	checkSlow(N == BevelLoop.NewMeshVertices.Num());
	for (int32 k = 0; k < N; ++k)
	{
		int32 Edge0 = BevelLoop.MeshEdges[k];
		int32 Edge1 = Mesh.FindEdge(BevelLoop.NewMeshVertices[k], BevelLoop.NewMeshVertices[(k + 1)%N]);
		BevelLoop.NewMeshEdges.Add(Edge1);
		checkSlow(Edge1 >= 0);
		if (Mesh.IsEdge(Edge1) && Edge0 != Edge1 && MeshEdgePairs.Contains(Edge0) == false )
		{
			MeshEdgePairs.Add(Edge0, Edge1);
			MeshEdgePairs.Add(Edge1, Edge0);
		}
	}
}

void FMeshBevel::UnlinkLoops(FDynamicMesh3& Mesh, FDynamicMeshChangeTracker* ChangeTracker)
{
	for (FBevelLoop& Loop : Loops)
	{
		UnlinkBevelLoop(Mesh, Loop, ChangeTracker);
	}
}





void FMeshBevel::UnlinkVertices(FDynamicMesh3& Mesh, FDynamicMeshChangeTracker* ChangeTracker)
{
	// TODO: currently have to do terminator vertices first because we do some of the 
	// determination inside the unlink code...

	for (FBevelVertex& Vertex : Vertices)
	{
		if (Vertex.VertexType == EBevelVertexType::TerminatorVertex)
		{
			UnlinkTerminatorVertex(Mesh, Vertex, ChangeTracker);
		}
	}

	for (FBevelVertex& Vertex : Vertices)
	{
		if (Vertex.VertexType == EBevelVertexType::JunctionVertex)
		{
			UnlinkJunctionVertex(Mesh, Vertex, ChangeTracker);
		}
	}
}


void FMeshBevel::UnlinkJunctionVertex(FDynamicMesh3& Mesh, FBevelVertex& Vertex, FDynamicMeshChangeTracker* ChangeTracker)
{
	check(Vertex.VertexType == EBevelVertexType::JunctionVertex);

	if (ChangeTracker)
	{
		ChangeTracker->SaveVertexOneRingTriangles(Vertex.VertexID, true);
	}

	int32 NumWedges = Vertex.Wedges.Num();
	checkSlow(NumWedges > 1);

	// Split triangles around vertex into separate tri-sets based on wedges.
	// This will create a new vertex for each wedge.
	for (int32 k = 1; k < NumWedges; ++k)
	{
		FOneRingWedge& Wedge = Vertex.Wedges[k];

		FDynamicMesh3::FVertexSplitInfo SplitInfo;
		EMeshResult Result = Mesh.SplitVertex(Vertex.VertexID, Wedge.Triangles, SplitInfo);
		if (Result == EMeshResult::Ok)
		{
			Wedge.WedgeVertex = SplitInfo.NewVertex;
		}
	}

	// update end start/end pairs for each wedge. If we created new edges above, this is
	// the first time we will encounter them, so save in edge correspondence map
	for (int32 k = 0; k < NumWedges; ++k)
	{
		FOneRingWedge& Wedge = Vertex.Wedges[k];
		for (int32 j = 0; j < 2; ++j)
		{
			int32 OldWedgeEdgeID = Wedge.BorderEdges[j];
			int32 OldWedgeEdgeIndex = Wedge.BorderEdgeTriEdgeIndices[j];
			int32 TriangleID = (j == 0) ? Wedge.Triangles[0] : Wedge.Triangles.Last();
			int32 CurWedgeEdgeID = Mesh.GetTriEdges(TriangleID)[OldWedgeEdgeIndex];
			FIndex3i TriVerts = Mesh.GetTriangle(TriangleID);
			if (OldWedgeEdgeID != CurWedgeEdgeID)
			{
				if (MeshEdgePairs.Contains(OldWedgeEdgeID) == false)
				{
					MeshEdgePairs.Add(OldWedgeEdgeID, CurWedgeEdgeID);
					MeshEdgePairs.Add(CurWedgeEdgeID, OldWedgeEdgeID);
				}
				Wedge.BorderEdges[j] = CurWedgeEdgeID;
			}
		}
	}

}




void FMeshBevel::UnlinkTerminatorVertex(FDynamicMesh3& Mesh, FBevelVertex& BevelVertex, FDynamicMeshChangeTracker* ChangeTracker)
{
	check(BevelVertex.VertexType == EBevelVertexType::TerminatorVertex);
	ensure(BevelVertex.Wedges.Num() == 2);

	if (ChangeTracker)
	{
		ChangeTracker->SaveVertexOneRingTriangles(BevelVertex.VertexID, true);
	}

	// TODO: do we need to update Wedge BorderEdges here??

	// split the vertex
	FDynamicMesh3::FVertexSplitInfo SplitInfo;
	EMeshResult Result = Mesh.SplitVertex(BevelVertex.VertexID, BevelVertex.Wedges[1].Triangles, SplitInfo);
	if (Result == EMeshResult::Ok)
	{
		BevelVertex.Wedges[1].WedgeVertex = SplitInfo.NewVertex;
	}

}



void FMeshBevel::DisplaceVertices(FDynamicMesh3& Mesh, double Distance)
{
	//
	// This displacement method produces not-very-nice-looking bevels, improvements TBD
	//

	auto GetDisplacedVertexPos = [Distance](const FDynamicMesh3& Mesh, int32 VertexID) -> FVector3d
	{
		FVector3d CurPos = Mesh.GetVertex(VertexID);
		FVector3d Centroid = FMeshWeights::MeanValueCentroid(Mesh, VertexID);
		FVector3d MoveDir = Normalized(Centroid - CurPos);
		return CurPos + Distance * MoveDir;
	};


	auto DisplacePairedVertexLists = [&GetDisplacedVertexPos](
		const FDynamicMesh3& Mesh, 
		TArray<int32>& Vertices0, TArray<int32>& Vertices1, 
		TArray<FVector3d>& Positions0, TArray<FVector3d>& Positions1, int32 InsetStart, int32 InsetEnd)
	{
		int32 NumVertices = Vertices0.Num();
		if (NumVertices == Vertices1.Num())
		{
			Positions0.SetNum(NumVertices);
			Positions1.SetNum(NumVertices);
			int32 Stop = NumVertices - InsetEnd;
			for (int32 k = InsetStart; k < Stop; ++k)
			{
				if (Vertices0[k] == Vertices1[k])
				{
					Positions0[k] = Positions1[k] = Mesh.GetVertex(Vertices0[k]);
				}
				else
				{
					Positions0[k] = GetDisplacedVertexPos(Mesh, Vertices0[k]);
					Positions1[k] = GetDisplacedVertexPos(Mesh, Vertices1[k]);
				}
			}
		}
	};


	for (FBevelEdge& Edge : Edges)
	{
		DisplacePairedVertexLists(Mesh, Edge.MeshVertices, Edge.NewMeshVertices, Edge.NewPositions0, Edge.NewPositions1, 
			Edge.bEndpointBoundaryFlag[0]?0:1, Edge.bEndpointBoundaryFlag[1]?0:1 );
	}
	for (FBevelLoop& Loop : Loops)
	{
		DisplacePairedVertexLists(Mesh, Loop.MeshVertices, Loop.NewMeshVertices, Loop.NewPositions0, Loop.NewPositions1, 0, 0);
	}


	// corners
	for (FBevelVertex& Vertex : Vertices)
	{
		if ( (Vertex.VertexType == EBevelVertexType::JunctionVertex)
			|| (Vertex.VertexType == EBevelVertexType::TerminatorVertex) )
		{
			int32 NumWedges = Vertex.Wedges.Num();
			for (int32 k = 0; k < NumWedges; ++k)
			{
				FOneRingWedge& Wedge = Vertex.Wedges[k];
				Wedge.NewPosition = GetDisplacedVertexPos(Mesh, Wedge.WedgeVertex);
			}
		}
	}


	auto SetDisplacedPositions = [&GetDisplacedVertexPos](FDynamicMesh3& Mesh, TArray<int32>& VerticesIn, TArray<FVector3d>& PositionsIn, int32 InsetStart, int32 InsetEnd)
	{
		int32 NumVertices = VerticesIn.Num();
		if (PositionsIn.Num() == NumVertices)
		{
			int32 Stop = NumVertices - InsetEnd;
			for (int32 k = InsetStart; k < Stop; ++k)
			{
				Mesh.SetVertex(VerticesIn[k], PositionsIn[k]);
			}
		}
	};


	// now bake in new positions
	for (FBevelEdge& Edge : Edges)
	{
		SetDisplacedPositions(Mesh, Edge.MeshVertices, Edge.NewPositions0, Edge.bEndpointBoundaryFlag[0]?0:1, Edge.bEndpointBoundaryFlag[1]?0:1);
		SetDisplacedPositions(Mesh, Edge.NewMeshVertices, Edge.NewPositions1, Edge.bEndpointBoundaryFlag[0]?0:1, Edge.bEndpointBoundaryFlag[1]?0:1);
	}
	for (FBevelLoop& Loop : Loops)
	{
		SetDisplacedPositions(Mesh, Loop.MeshVertices, Loop.NewPositions0, 0, 0);
		SetDisplacedPositions(Mesh, Loop.NewMeshVertices, Loop.NewPositions1, 0, 0);
	}
	for (FBevelVertex& Vertex : Vertices)
	{
		if ( (Vertex.VertexType == EBevelVertexType::JunctionVertex)
			|| (Vertex.VertexType == EBevelVertexType::TerminatorVertex) )
		{
			for (FOneRingWedge& Wedge : Vertex.Wedges)
			{
				Mesh.SetVertex(Wedge.WedgeVertex, Wedge.NewPosition);
			}
		}
	}
}




void FMeshBevel::AppendJunctionVertexPolygon(FDynamicMesh3& Mesh, FBevelVertex& Vertex)
{
	check(Vertex.VertexType == EBevelVertexType::JunctionVertex);

	// UnlinkJunctionVertex() split the terminator vertex into N vertices, one for each
	// (now disconnected) triangle-wedge. The wedges are ordered such that their wedge-vertices
	// define a polygon with correct winding, so we can just mesh it and append the triangles

	TArray<FVector3d> PolygonPoints;
	for (FOneRingWedge& Wedge : Vertex.Wedges)
	{
		PolygonPoints.Add(Mesh.GetVertex(Wedge.WedgeVertex));
	}

	TArray<FIndex3i> Triangles;
	PolygonTriangulation::TriangulateSimplePolygon<double>(PolygonPoints, Triangles);
	Vertex.NewGroupID = Mesh.AllocateTriangleGroup();
	for (FIndex3i Tri : Triangles)
	{
		int32 A = Vertex.Wedges[Tri.A].WedgeVertex;
		int32 B = Vertex.Wedges[Tri.B].WedgeVertex;
		int32 C = Vertex.Wedges[Tri.C].WedgeVertex;
		int32 tid = Mesh.AppendTriangle(A, B, C, Vertex.NewGroupID);
		if (Mesh.IsTriangle(tid))
		{
			Vertex.NewTriangles.Add(tid);
		}
	}
}


void FMeshBevel::AppendTerminatorVertexTriangle(FDynamicMesh3& Mesh, FBevelVertex& Vertex)
{
	check(Vertex.VertexType == EBevelVertexType::TerminatorVertex);

	// UnlinkTerminatorVertex() opened up a triangle-shaped hole adjacent to the incoming edge quad-strip
	// at the terminator vertex. The Wedges of the terminator vertex contain the vertex IDs of the two
	// verts on the quad-strip edge. We need the third vertex. We stored [SplitEdge, FarVertexID] in
	// .TerminatorInfo, however FarVertexID may have become a different vertex when we unlinked other
	// vertices. So, we will try to use SplitEdge to find it.
	// If this turns out to have problems, basically the QuadEdgeID is on the boundary of a 3-edge hole,
	// and so it should be straightforward to find the two other boundary edges and that gives the vertex.
	int32 RingSplitEdgeID = Vertex.TerminatorInfo.A;
	if (Mesh.IsEdge(RingSplitEdgeID))
	{
		FIndex2i SplitEdgeV = Mesh.GetEdgeV(RingSplitEdgeID);
		int32 FarVertexID = SplitEdgeV.OtherElement(Vertex.VertexID);

		int32 QuadEdgeID = Mesh.FindEdge(Vertex.Wedges[0].WedgeVertex, Vertex.Wedges[1].WedgeVertex);
		if (Mesh.IsEdge(QuadEdgeID))
		{
			FIndex2i QuadEdgeV = Mesh.GetOrientedBoundaryEdgeV(QuadEdgeID);
			// should have computed this GroupID in initial setup
			int32 UseGroupID = (Vertex.NewGroupID >= 0) ? Vertex.NewGroupID : Mesh.AllocateTriangleGroup();
			int32 tid = Mesh.AppendTriangle(QuadEdgeV.B, QuadEdgeV.A, FarVertexID, UseGroupID);
			if (Mesh.IsTriangle(tid))
			{
				Vertex.NewTriangles.Add(tid);
			}
		}
	}
}


void FMeshBevel::AppendEdgeQuads(FDynamicMesh3& Mesh, FBevelEdge& Edge)
{
	int32 NumEdges = Edge.MeshEdges.Num();
	if (NumEdges != Edge.NewMeshEdges.Num())
	{
		return;
	}

	Edge.NewGroupID = Mesh.AllocateTriangleGroup();

	// At this point each edge-span should be fully disconnected into a set of paired edges, 
	// so we can trivially join each edge pair with a quad.
	for (int32 k = 0; k < NumEdges; ++k)
	{
		int32 EdgeID0 = Edge.MeshEdges[k];
		int32 EdgeID1 = Edge.NewMeshEdges[k];

		// in certain cases, like bevel topo-edges with a single mesh-edge, we would not
		// have been able to construct the "other" mesh edge when processing the topo-edge
		// (where .NewMeshEdges is computed), it would only have been created when processing the
		// junction vertex. Currently we do not go back and update .NewMeshEdges in that case, but
		// we do store the edge-pair-correspondence in the MeshEdgePairs map. 
		if (EdgeID0 == EdgeID1)
		{
			int32* FoundEdgeID1 = MeshEdgePairs.Find(EdgeID0);
			if (FoundEdgeID1 != nullptr)
			{
				EdgeID1 = *FoundEdgeID1;
			}
		}

		FIndex2i QuadTris(IndexConstants::InvalidID, IndexConstants::InvalidID);
		if (EdgeID0 != EdgeID1 && Mesh.IsEdge(EdgeID1) )
		{
			FIndex2i EdgeV0 = Mesh.GetOrientedBoundaryEdgeV(EdgeID0);
			FIndex2i EdgeV1 = Mesh.GetOrientedBoundaryEdgeV(EdgeID1);
			QuadTris.A = Mesh.AppendTriangle(EdgeV0.B, EdgeV0.A, EdgeV1.B, Edge.NewGroupID);
			QuadTris.B = Mesh.AppendTriangle(EdgeV1.B, EdgeV1.A, EdgeV0.B, Edge.NewGroupID);
		}
		Edge.StripQuads.Add(QuadTris);
	}
}



void FMeshBevel::AppendLoopQuads(FDynamicMesh3& Mesh, FBevelLoop& Loop)
{
	int32 NumEdges = Loop.MeshEdges.Num();
	if (NumEdges != Loop.NewMeshEdges.Num())
	{
		return;
	}

	Loop.NewGroupID = Mesh.AllocateTriangleGroup();

	// At this point each edge-span should be fully disconnected into a set of paired edges, 
	// so we can trivially join each edge pair with a quad.
	for (int32 k = 0; k < NumEdges; ++k)
	{
		int32 EdgeID0 = Loop.MeshEdges[k];
		int32 EdgeID1 = Loop.NewMeshEdges[k];

		// case that happens in AppendEdgeQuads() should never happen for loops...

		FIndex2i QuadTris(IndexConstants::InvalidID, IndexConstants::InvalidID);
		if (EdgeID0 != EdgeID1 && Mesh.IsEdge(EdgeID1))
		{
			FIndex2i EdgeV0 = Mesh.GetOrientedBoundaryEdgeV(EdgeID0);
			FIndex2i EdgeV1 = Mesh.GetOrientedBoundaryEdgeV(EdgeID1);
			QuadTris.A = Mesh.AppendTriangle(EdgeV0.B, EdgeV0.A, EdgeV1.B, Loop.NewGroupID);
			QuadTris.B = Mesh.AppendTriangle(EdgeV1.B, EdgeV1.A, EdgeV0.B, Loop.NewGroupID);
		}
		Loop.StripQuads.Add(QuadTris);
	}
}




void FMeshBevel::CreateBevelMeshing(FDynamicMesh3& Mesh)
{
	for (FBevelVertex& Vertex : Vertices)
	{
		if (Vertex.VertexType == EBevelVertexType::JunctionVertex)
		{
			if (Vertex.Wedges.Num() > 2)
			{
				AppendJunctionVertexPolygon(Mesh, Vertex);
			}
		}
	}

	for (FBevelEdge& Edge : Edges)
	{
		AppendEdgeQuads(Mesh, Edge);
	}
	for (FBevelLoop& Loop : Loops)
	{
		AppendLoopQuads(Mesh, Loop);
	}

	// easier to do these last so that we can use quad edge to orient the triangle
	for (FBevelVertex& Vertex : Vertices)
	{
		if (Vertex.VertexType == EBevelVertexType::TerminatorVertex)
		{
			AppendTerminatorVertexTriangle(Mesh, Vertex);
		}
	}
}




void FMeshBevel::ComputeNormals(FDynamicMesh3& Mesh)
{
	if (Mesh.HasAttributes() == false)
	{
		return;
	}
	FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();

	auto SetNormalsOnTriRegion = [NormalOverlay](const TArray<int32>& Triangles)
	{
		if (Triangles.Num() > 0)
		{
			FMeshNormals::InitializeOverlayRegionToPerVertexNormals(NormalOverlay, Triangles);
		}
	};

	auto QuadsToTris = [](const FDynamicMesh3& Mesh, const TArray<FIndex2i>& Quads, TArray<int32>& TrisOut)
	{
		int32 N = Quads.Num();
		TrisOut.Reset();
		TrisOut.Reserve(2 * N);
		for (const FIndex2i& Quad : Quads)
		{
			if (Mesh.IsTriangle(Quad.A))
			{
				TrisOut.Add(Quad.A);
			}
			if (Mesh.IsTriangle(Quad.B))
			{
				TrisOut.Add(Quad.B);
			}
		}
	};


	for (FBevelVertex& Vertex : Vertices)
	{
		SetNormalsOnTriRegion(Vertex.NewTriangles);
	}

	TArray<int32> TriList;
	for (FBevelEdge& Edge : Edges)
	{
		QuadsToTris(Mesh, Edge.StripQuads, TriList);
		SetNormalsOnTriRegion(TriList);
	}
	for (FBevelLoop& Loop : Loops)
	{
		QuadsToTris(Mesh, Loop.StripQuads, TriList);
		SetNormalsOnTriRegion(TriList);
	}
}
