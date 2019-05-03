// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "MeshRegionBoundaryLoops.h"
#include "MeshBoundaryLoops.h"   // has a set of internal static functions we re-use
#include "VectorUtil.h"



FMeshRegionBoundaryLoops::FMeshRegionBoundaryLoops(const FDynamicMesh3* MeshIn, const TArray<int>& RegionTris, bool bAutoCompute)
{
	this->Mesh = MeshIn;

	// make flag set for included triangles
	triangles.Init(false, Mesh->MaxTriangleID());
	for (int i = 0; i < RegionTris.Num(); ++i)
	{
		triangles[RegionTris[i]] = true;
	}

	// make flag set for included edges
	// NOTE: this currently processes non-boundary-edges twice. Could
	// avoid w/ another IndexFlagSet, but the check is inexpensive...
	edges.Init(false, Mesh->MaxEdgeID());
	for (int i = 0; i < RegionTris.Num(); ++i)
	{
		int tid = RegionTris[i];
		FIndex3i te = Mesh->GetTriEdges(tid);
		for (int j = 0; j < 3; ++j)
		{
			int eid = te[j];
			if (!ContainsElement(edges, eid))
			{
				FIndex2i et = Mesh->GetEdgeT(eid);
				if (et.B == IndexConstants::InvalidID || triangles[et.A] != triangles[et.B])
				{
					edges_roi.Add(eid);
					edges[eid] = true;
				}
			}
		}
	}

	if (bAutoCompute)
	{
		Compute();
	}
}



int FMeshRegionBoundaryLoops::GetMaxVerticesLoopIndex() const
{
	int j = 0;
	for (int i = 1; i < Loops.Num(); ++i)
	{
		if (Loops[i].Vertices.Num() > Loops[j].Vertices.Num())
		{
			j = i;
		}
	}
	return j;
}



bool FMeshRegionBoundaryLoops::Compute()
{
	// This algorithm assumes that triangles are oriented consistently, 
	// so closed boundary-loop can be followed by walking edges in-order
	Loops.SetNum(0);

	// Temporary memory used to indicate when we have "used" an edge.
	TArray<bool> used_edge;
	used_edge.Init(false, Mesh->MaxEdgeID());

	// current loop is stored here, cleared after each loop extracted
	TArray<int> loop_edges;
	TArray<int> loop_verts;
	TArray<int> bowties;

	// Temp buffer for reading back all boundary edges of a vertex.
	// probably always small but : pathological cases it could be large...
	TArray<int> all_e;
	all_e.SetNum(16);

	// process all edges of mesh
	for (int eid : edges_roi)
	{
		if (used_edge[eid] == true)
		{
			continue;
		}
		if (IsEdgeOnBoundary(eid) == false)
		{
			continue;
		}

		// ok this is start of a boundary chain
		int eStart = eid;
		used_edge[eStart] = true;
		loop_edges.Add(eStart);

		int eCur = eid;

		// follow the chain : order of oriented edges
		bool bClosed = false;
		while (!bClosed)
		{

			// [TODO] can do this more efficiently?
			int tid_in = IndexConstants::InvalidID, tid_out = IndexConstants::InvalidID;
			IsEdgeOnBoundary(eCur, tid_in, tid_out);

			FIndex2i ev = GetOrientedEdgeVerts(eCur, tid_in, tid_out);
			int cure_a = ev.A, cure_b = ev.B;
			loop_verts.Add(cure_a);

			int e0 = -1, e1 = 1;
			int bdry_nbrs = GetVertexBoundaryEdges(cure_b, e0, e1);

			check(bdry_nbrs >= 2); //  if (bdry_nbrs < 2) throw new MeshBoundaryLoopsException("MeshRegionBoundaryLoops.Compute: found broken neighbourhood at vertex " + cure_b){ UnclosedLoop = true };

			int eNext = -1;
			if (bdry_nbrs > 2)
			{
				// found "bowtie" vertex...things just got complicated!

				if (cure_b == loop_verts[0])
				{
					// The "end" of the current edge is the same as the start vertex.
					// This means we can close the loop here. Might as well!
					eNext = -2;   // sentinel value used below

				}
				else
				{
					// try to find an unused outgoing edge that is oriented properly.
					// This could create sub-loops, we will handle those later
					if (bdry_nbrs >= all_e.Num())
						all_e.SetNum(bdry_nbrs);
					int num_be = GetAllVertexBoundaryEdges(cure_b, all_e);

					check(num_be == bdry_nbrs);

					// Try to pick the best "turn left" vertex.
					eNext = FindLeftTurnEdge(eCur, cure_b, all_e, num_be, used_edge);

					check(eNext != -1); // throw new MeshBoundaryLoopsException("MeshRegionBoundaryLoops.Compute: cannot find valid outgoing edge at bowtie vertex " + cure_b){ BowtieFailure = true };
				}

				if (bowties.Contains(cure_b) == false)
				{
					bowties.Add(cure_b);
				}

			}
			else
			{
				check(e0 == eCur || e1 == eCur);
				eNext = (e0 == eCur) ? e1 : e0;
			}

			if (eNext == -2)
			{
				// found a bowtie vert that is the same as start-of-loop, so we
				// are just closing it off explicitly
				bClosed = true;
			}
			else if (eNext == eStart)
			{
				// found edge at start of loop, so loop is done.
				bClosed = true;
			}
			else
			{
				// push onto accumulated list
				check(used_edge[eNext] == false);
				loop_edges.Add(eNext);
				eCur = eNext;
				used_edge[eCur] = true;
			}
		}

		// if we saw a bowtie vertex, we might need to break up this loop,
		// so call ExtractSubloops
		if (bowties.Num() > 0)
		{
			TArray<FEdgeLoop> subloops = ExtractSubloops(loop_verts, loop_edges, bowties);
			for (int i = 0; i < subloops.Num(); ++i)
			{
				Loops.Add(subloops[i]);
			}
		}
		else
		{
			// clean simple loop, convert to FEdgeLoop instance
			FEdgeLoop loop(Mesh);
			loop.Vertices = loop_verts;
			loop.Edges = loop_edges;
			Loops.Add(loop);
		}

		// reset these lists
		loop_edges.SetNum(0);
		loop_verts.SetNum(0);
		bowties.SetNum(0);
	}

	return true;
}






// returns true for both internal and mesh boundary edges
// tid_in and tid_out are triangles 'in' and 'out' of set, respectively
bool FMeshRegionBoundaryLoops::IsEdgeOnBoundary(int eid, int& tid_in, int& tid_out) const
{
	if (ContainsElement(edges, eid) == false)
	{
		return false;
	}

	tid_in = tid_out = IndexConstants::InvalidID;
	FIndex2i et = Mesh->GetEdgeT(eid);
	if (et.B == IndexConstants::InvalidID)	// boundary edge!
	{
		tid_in = et.A;
		tid_out = et.B;
		return true;
	}

	bool in0 = triangles[et.A];
	bool in1 = triangles[et.B];
	if (in0 != in1)
	{
		tid_in = (in0) ? et.A : et.B;
		tid_out = (in0) ? et.B : et.A;
		return true;
	}
	return false;
}



// return same indices as GetEdgeV, but oriented based on attached triangle
FIndex2i FMeshRegionBoundaryLoops::GetOrientedEdgeVerts(int eID, int tid_in, int tid_out)
{
	FIndex2i edgev = Mesh->GetEdgeV(eID);
	int a = edgev.A, b = edgev.B;
	FIndex3i tri = Mesh->GetTriangle(tid_in);
	int ai = IndexUtil::FindEdgeIndexInTri(a, b, tri);
	return FIndex2i(tri[ai], tri[(ai + 1) % 3]);
}


int FMeshRegionBoundaryLoops::GetVertexBoundaryEdges(int vID, int& e0, int& e1)
{
	int count = 0;
	for (int eid : Mesh->VtxEdgesItr(vID))
	{
		if (IsEdgeOnBoundary(eid))
		{
			if (count == 0)
			{
				e0 = eid;
			}
			else if (count == 1)
			{
				e1 = eid;
			}
			count++;
		}
	}
	return count;
}


int FMeshRegionBoundaryLoops::GetAllVertexBoundaryEdges(int vID, TArray<int>& e)
{
	int count = 0;
	for (int eid : Mesh->VtxEdgesItr(vID))
	{
		if (IsEdgeOnBoundary(eid))
		{
			e[count++] = eid;
		}
	}
	return count;
}


FVector3d FMeshRegionBoundaryLoops::GetVertexNormal(int vid)
{
	FVector3d n = FVector3d::Zero();
	for (int ti : Mesh->VtxTrianglesItr(vid))
	{
		n += Mesh->GetTriNormal(ti);
	}
	n.Normalize();
	return n;
}



//
// [TODO] for internal vertices, there is no ambiguity : which is the left-turn edge,
//   we should be using 'closest' left-neighbour edge.
//
// ok, bdry_edges[0...bdry_edges_count] contains the boundary edges coming out of bowtie_v.
// We want to pick the best one to continue the loop that came : to bowtie_v on incoming_e.
// If the loops are all sane, then we will get the smallest loops by "turning left" at bowtie_v.
// So, we compute the tangent plane at bowtie_v, and then the signed angle for each
// viable edge : this plane. 
int FMeshRegionBoundaryLoops::FindLeftTurnEdge(int incoming_e, int bowtie_v, TArray<int>& bdry_edges, int bdry_edges_count, TArray<bool>& used_edges)
{
	// compute normal and edge [a,bowtie]
	FVector3d n = GetVertexNormal(bowtie_v);
	//int other_v = Mesh->edge_other_v(incoming_e, bowtie_v);
	FIndex2i ev = Mesh->GetEdgeV(incoming_e);
	int other_v = (ev.A == bowtie_v) ? ev.B : ev.A;
	FVector3d ab = Mesh->GetVertex(bowtie_v) - Mesh->GetVertex(other_v);

	// our winner
	int best_e = -1;
	double best_angle = TNumericLimits<double>::Max();

	for (int i = 0; i < bdry_edges_count; ++i)
	{
		int bdry_eid = bdry_edges[i];
		if (used_edges[bdry_eid] == true)
			continue;       // this edge is already used

		// [TODO] can do this more efficiently?
		int tid_in = IndexConstants::InvalidID, tid_out = IndexConstants::InvalidID;
		IsEdgeOnBoundary(bdry_eid, tid_in, tid_out);
		FIndex2i bdry_ev = GetOrientedEdgeVerts(bdry_eid, tid_in, tid_out);
		//FIndex2i bdry_ev = Mesh.GetOrientedBoundaryEdgeV(bdry_eid);

		if (bdry_ev.A != bowtie_v) {
			continue;       // have to be able to chain to end of current edge, orientation-wise
		}

		// compute projected angle
		FVector3d bc = Mesh->GetVertex(bdry_ev.B) - Mesh->GetVertex(bowtie_v);
		double fAngleS = VectorUtil::PlaneAngleSignedD(ab, bc, n);

		// turn left!
		if (best_angle == TNumericLimits<double>::Max() || fAngleS < best_angle)
		{
			best_angle = fAngleS;
			best_e = bdry_eid;
		}
	}
	check(best_e != -1);

	return best_e;
}




// This is called when loopV contains one or more "bowtie" vertices.
// These vertices *might* be duplicated : loopV (but not necessarily)
// If they are, we have to break loopV into subloops that don't contain duplicates.
//
// The list bowties contains all the possible duplicates 
// (all v in bowties occur in loopV at least once)
//
// Currently loopE is not used, and the returned FEdgeLoop objects do not have their Edges
// arrays initialized. Perhaps to improve : future.
TArray<FEdgeLoop> FMeshRegionBoundaryLoops::ExtractSubloops(TArray<int>& loopV, const TArray<int>& loopE, const TArray<int>& bowties)
{
	TArray<FEdgeLoop> subs;

	// figure out which bowties we saw are actually duplicated : loopV
	TArray<int> dupes;
	for (int bv : bowties)
	{
		if (FMeshBoundaryLoops::CountInList(loopV, bv) > 1)
		{
			dupes.Add(bv);
		}
	}

	// we might not actually have any duplicates, if we got luck. Early out : that case
	if (dupes.Num() == 0)
	{
		FEdgeLoop NewLoop(Mesh);
		NewLoop.Vertices = loopV;
		NewLoop.Edges = loopE;
		NewLoop.BowtieVertices = bowties;
		subs.Add(NewLoop);
		return subs;
	}

	// This loop extracts subloops until we have dealt with all the
	// duplicate vertices : loopV
	while (dupes.Num() > 0)
	{

		// Find shortest "simple" loop, ie a loop from a bowtie to itself that
		// does not contain any other bowties. This is an independent loop.
		// We're doing a lot of extra work here if we only have one element : dupes...
		int bi = 0, bv = 0;
		int start_i = -1, end_i = -1;
		int bv_shortest = -1; int shortest = TNumericLimits<int>::Max();
		for (; bi < dupes.Num(); ++bi)
		{
			bv = dupes[bi];
			if (FMeshBoundaryLoops::IsSimpleBowtieLoop(loopV, dupes, bv, start_i, end_i))
			{
				int len = FMeshBoundaryLoops::CountSpan(loopV, start_i, end_i);
				if (len < shortest)
				{
					bv_shortest = bv;
					shortest = len;
				}
			}
		}
		check(bv_shortest != -1); //  throw new MeshBoundaryLoopsException("MeshRegionBoundaryLoops.Compute: Cannot find a valid simple loop");
		if (bv != bv_shortest)
		{
			bv = bv_shortest;
			// running again just to get start_i and end_i...
			FMeshBoundaryLoops::IsSimpleBowtieLoop(loopV, dupes, bv, start_i, end_i);
		}

		check(loopV[start_i] == bv && loopV[end_i] == bv);

		FEdgeLoop loop(Mesh);
		FMeshBoundaryLoops::ExtractSpan(loopV, start_i, end_i, true, loop.Vertices);
		FEdgeLoop::VertexLoopToEdgeLoop(Mesh, loop.Vertices, loop.Edges);
		loop.BowtieVertices = bowties;
		subs.Add(loop);

		// If there are no more duplicates of this bowtie, we can treat
		// it like a regular vertex now
		if (FMeshBoundaryLoops::CountInList(loopV, bv) < 2)
		{
			dupes.Remove(bv);
		}
	}

	// Should have one loop left that contains duplicates. 
	// Extract this as a separate loop
	int nLeft = 0;
	for (int i = 0; i < loopV.Num(); ++i)
	{
		if (loopV[i] != -1)
		{
			nLeft++;
		}
	}
	if (nLeft > 0)
	{
		FEdgeLoop loop(Mesh);
		loop.Vertices.SetNum(nLeft);
		int vi = 0;
		for (int i = 0; i < loopV.Num(); ++i)
		{
			if (loopV[i] != -1)
			{
				loop.Vertices[vi++] = loopV[i];
			}
		}
		FEdgeLoop::VertexLoopToEdgeLoop(Mesh, loop.Vertices, loop.Edges);
		loop.BowtieVertices = bowties;
		subs.Add(loop);
	}

	return subs;
}