// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DynamicMesh3.h"


FIndex2i FDynamicMesh3::GetEdgeOpposingV(int eID) const
{
	// [TODO] there was a comment here saying this does more work than necessary??
	// ** it is important that verts returned maintain [c,d] order!!
	int i = 4 * eID;
	int a = Edges[i], b = Edges[i + 1];
	int t0 = Edges[i + 2], t1 = Edges[i + 3];
	int c = IndexUtil::FindTriOtherVtx(a, b, Triangles, t0);
	if (t1 != InvalidID) 
	{
		int d = IndexUtil::FindTriOtherVtx(a, b, Triangles, t1);
		return FIndex2i(c, d);
	}
	else
	{
		return FIndex2i(c, InvalidID);
	}
}


int FDynamicMesh3::GetVtxBoundaryEdges(int vID, int& e0, int& e1) const
{
	if (VertexRefCounts.IsValid(vID)) 
	{
		int count = 0;
		for (int eid : VertexEdgeLists.Values(vID)) 
		{
			int ei = 4 * eid;
			if (Edges[ei + 3] == InvalidID) 
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
	check(false);
	return -1;
}


int FDynamicMesh3::GetAllVtxBoundaryEdges(int vID, TArray<int>& EdgeListOut) const
{
	if (VertexRefCounts.IsValid(vID)) 
	{
		int count = 0;
		for (int eid : VertexEdgeLists.Values(vID)) 
		{
			int ei = 4 * eid;
			if (Edges[ei + 3] == InvalidID)
			{
				EdgeListOut.Add(eid);
				count++;
			}
		}
		return count;
	}
	check(false);
	return -1;
}



void FDynamicMesh3::GetVtxNbrhood(int eID, int vID, int& vOther, int& oppV1, int& oppV2, int& t1, int& t2) const
{
	int i = 4 * eID;
	vOther = (Edges[i] == vID) ? Edges[i + 1] : Edges[i];
	t1 = Edges[i + 2];
	oppV1 = IndexUtil::FindTriOtherVtx(vID, vOther, Triangles, t1);
	t2 = Edges[i + 3];
	if (t2 != InvalidID)
	{
		oppV2 = IndexUtil::FindTriOtherVtx(vID, vOther, Triangles, t2);
	}
	else
	{
		t2 = InvalidID;
	}
}


int FDynamicMesh3::GetVtxTriangleCount(int vID, bool bBruteForce) const
{
	if (bBruteForce) 
	{
		TArray<int> vTriangles;
		if (GetVtxTriangles(vID, vTriangles, false) != EMeshResult::Ok)
		{
			return -1;
		}
		return (int)vTriangles.Num();
	}

	if (!IsVertex(vID))
	{
		return -1;
	}
	int N = 0;
	for (int eid : VertexEdgeLists.Values(vID)) 
	{
		int vOther = GetOtherEdgeVertex(eid, vID);
		int i = 4 * eid;
		int et0 = Edges[i + 2];
		if (TriHasSequentialVertices(et0, vID, vOther))
		{
			N++;
		}
		int et1 = Edges[i + 3];
		if (et1 != InvalidID && TriHasSequentialVertices(et1, vID, vOther))
		{
			N++;
		}
	}
	return N;
}



EMeshResult FDynamicMesh3::GetVtxTriangles(int vID, TArray<int>& TrianglesOut, bool bUseOrientation) const
{
	if (!IsVertex(vID))
	{
		return EMeshResult::Failed_NotAVertex;
	}

	if (bUseOrientation) 
	{
		for (int eid : VertexEdgeLists.Values(vID)) 
		{
			int vOther = GetOtherEdgeVertex(eid, vID);
			int i = 4 * eid;
			int et0 = Edges[i + 2];
			if (TriHasSequentialVertices(et0, vID, vOther))
			{
				TrianglesOut.Add(et0);
			}
			int et1 = Edges[i + 3];
			if (et1 != InvalidID && TriHasSequentialVertices(et1, vID, vOther))
			{
				TrianglesOut.Add(et1);
			}
		}
	}
	else 
	{
		// brute-force method
		for (int eid : VertexEdgeLists.Values(vID)) 
		{
			int i = 4 * eid;
			int t0 = Edges[i + 2];
			TrianglesOut.AddUnique(t0);

			int t1 = Edges[i + 3];
			if (t1 != InvalidID)
			{
				TrianglesOut.AddUnique(t1);
			}
		}
	}
	return EMeshResult::Ok;
}





bool FDynamicMesh3::IsBoundaryVertex(int vID) const 
{
	check(IsVertex(vID));
	for (int eid : VertexEdgeLists.Values(vID)) 
	{
		if (Edges[4 * eid + 3] == InvalidID)
		{
			return true;
		}
	}
	return false;
}



bool FDynamicMesh3::IsBoundaryTriangle(int tID) const
{
	check(IsTriangle(tID));
	int i = 3 * tID;
	return IsBoundaryEdge(TriangleEdges[i]) || IsBoundaryEdge(TriangleEdges[i + 1]) || IsBoundaryEdge(TriangleEdges[i + 2]);
}




FIndex2i FDynamicMesh3::GetOrientedBoundaryEdgeV(int eID) const
{
	if (EdgeRefCounts.IsValid(eID)) 
	{
		int ei = 4 * eID;
		if (Edges[ei + 3] == InvalidID) 
		{
			int a = Edges[ei], b = Edges[ei + 1];
			int ti = 3 * Edges[ei + 2];
			FIndex3i tri(Triangles[ti], Triangles[ti + 1], Triangles[ti + 2]);
			int ai = IndexUtil::FindEdgeIndexInTri(a, b, tri);
			return FIndex2i(tri[ai], tri[(ai + 1) % 3]);
		}
	}
	check(false);
	return InvalidEdge();
}


bool FDynamicMesh3::IsGroupBoundaryEdge(int eID) const
{
	check(IsEdge(eID));
	check(HasTriangleGroups());

	int et1 = Edges[4 * eID + 3];
	if (et1 == InvalidID)
	{
		return false;
	}
	int g1 = (*TriangleGroups)[et1];
	int et0 = Edges[4 * eID + 2];
	int g0 = (*TriangleGroups)[et0];
	return g1 != g0;
}



bool FDynamicMesh3::IsGroupBoundaryVertex(int vID) const
{
	check(IsVertex(vID));
	check(HasTriangleGroups());

	int group_id = InvalidGroupID;
	for (int eID : VertexEdgeLists.Values(vID)) 
	{
		int et0 = Edges[4 * eID + 2];
		int g0 = (*TriangleGroups)[et0];
		if (group_id != g0) 
		{
			if (group_id == InvalidGroupID)
			{
				group_id = g0;
			}
			else
			{
				return true;        // saw multiple group IDs
			}
		}
		int et1 = Edges[4 * eID + 3];
		if (et1 != InvalidID) 
		{
			int g1 = (*TriangleGroups)[et1];
			if (group_id != g1)
			{
				return true;        // saw multiple group IDs
			}
		}
	}
	return false;
}



bool FDynamicMesh3::IsGroupJunctionVertex(int vID) const
{
	check(IsVertex(vID));
	check(HasTriangleGroups());

	FIndex2i groups(InvalidGroupID, InvalidGroupID);
	for (int eID : VertexEdgeLists.Values(vID)) 
	{
		FIndex2i et(Edges[4 * eID + 2], Edges[4 * eID + 3]);
		for (int k = 0; k < 2; ++k) 
		{
			if (et[k] == InvalidID)
			{
				continue;
			}
			int g0 = (*TriangleGroups)[et[k]];
			if (g0 != groups[0] && g0 != groups[1]) 
			{
				if (groups[0] != InvalidGroupID && groups[1] != InvalidGroupID)
				{
					return true;
				}
				if (groups[0] == InvalidGroupID)
				{
					groups[0] = g0;
				}
				else
				{
					groups[1] = g0;
				}
			}
		}
	}
	return false;
}


bool FDynamicMesh3::GetVertexGroups(int vID, FIndex4i& groups) const
{
	check(IsVertex(vID));
	check(HasTriangleGroups());

	groups = FIndex4i(InvalidGroupID, InvalidGroupID, InvalidGroupID, InvalidGroupID);
	int ng = 0;

	for (int eID : VertexEdgeLists.Values(vID)) 
	{
		int et0 = Edges[4 * eID + 2];
		int g0 = (*TriangleGroups)[et0];
		if (groups.Contains(g0) == false)
		{
			groups[ng++] = g0;
		}
		if (ng == 4)
		{
			return false;
		}
		int et1 = Edges[4 * eID + 3];
		if (et1 != InvalidID) 
		{
			int g1 = (*TriangleGroups)[et1];
			if (groups.Contains(g1) == false)
			{
				groups[ng++] = g1;
			}
			if (ng == 4)
			{
				return false;
			}
		}
	}
	return true;
}



bool FDynamicMesh3::GetAllVertexGroups(int vID, TArray<int>& GroupsOut) const
{
	check(IsVertex(vID));
	check(HasTriangleGroups());

	for (int eID : VertexEdgeLists.Values(vID)) 
	{
		int et0 = Edges[4 * eID + 2];
		int g0 = (*TriangleGroups)[et0];
		GroupsOut.AddUnique(g0);

		int et1 = Edges[4 * eID + 3];
		if (et1 != InvalidID) 
		{
			int g1 = (*TriangleGroups)[et1];
			GroupsOut.AddUnique(g1);
		}
	}
	return true;
}




/**
 * returns true if vID is a "bowtie" vertex, ie multiple disjoint triangle sets in one-ring
 */
bool FDynamicMesh3::IsBowtieVertex(int vID) const
{
	check(VertexRefCounts.IsValid(vID));

	int nEdges = VertexEdgeLists.GetCount(vID);
	if (nEdges == 0)
	{
		return false;
	}

	// find a boundary edge to start at
	int start_eid = -1;
	bool start_at_boundary = false;
	for (int eid : VertexEdgeLists.Values(vID)) 
	{
		if (Edges[4 * eid + 3] == InvalidID) 
		{
			start_at_boundary = true;
			start_eid = eid;
			break;
		}
	}
	// if no boundary edge, start at arbitrary edge
	if (start_eid == -1)
	{
		start_eid = VertexEdgeLists.First(vID);
	}
	// initial triangle
	int start_tid = Edges[4 * start_eid + 2];

	int prev_tid = start_tid;
	int prev_eid = start_eid;

	// walk forward to next edge. if we hit start edge or boundary edge,
	// we are done the walk. count number of edges as we go.
	int count = 1;
	while (true) 
	{
		int i = 3 * prev_tid;
		FIndex3i tv(Triangles[i], Triangles[i + 1], Triangles[i + 2]);
		FIndex3i te(TriangleEdges[i], TriangleEdges[i + 1], TriangleEdges[i + 2]);
		int vert_idx = IndexUtil::FindTriIndex(vID, tv);
		int e1 = te[vert_idx], e2 = te[(vert_idx + 2) % 3];
		int next_eid = (e1 == prev_eid) ? e2 : e1;
		if (next_eid == start_eid)
		{
			break;
		}
		FIndex2i next_eid_tris = GetEdgeT(next_eid);
		int next_tid = (next_eid_tris[0] == prev_tid) ? next_eid_tris[1] : next_eid_tris[0];
		if (next_tid == InvalidID) 
		{
			break;
		}
		prev_eid = next_eid;
		prev_tid = next_tid;
		count++;
	}

	// if we did not see all edges at vertex, we have a bowtie
	int target_count = (start_at_boundary) ? nEdges - 1 : nEdges;
	bool is_bowtie = (target_count != count);
	return is_bowtie;
}




int FDynamicMesh3::FindTriangle(int a, int b, int c) const
{
	int eid = FindEdge(a, b);
	if (eid == InvalidID)
	{
		return InvalidID;
	}
	int ei = 4 * eid;

	// triangles attached to edge [a,b] must contain verts a and b...
	int ti = 3 * Edges[ei + 2];
	if (Triangles[ti] == c || Triangles[ti + 1] == c || Triangles[ti + 2] == c)
	{
		return Edges[ei + 2];
	}
	if (Edges[ei + 3] != InvalidID) 
	{
		ti = 3 * Edges[ei + 3];
		if (Triangles[ti] == c || Triangles[ti + 1] == c || Triangles[ti + 2] == c)
		{
			return Edges[ei + 3];
		}
	}

	return InvalidID;
}



/**
 * Computes bounding box of all vertices.
 */
FAxisAlignedBox3d FDynamicMesh3::GetBounds() const
{
	double x = 0, y = 0, z = 0;
	for (int vi : VertexIndicesItr())     // find initial valid vertex
	{
		int k = 3 * vi;
		x = Vertices[k]; y = Vertices[k + 1]; z = Vertices[k + 2];
		break;
	}
	double minx = x, maxx = x, miny = y, maxy = y, minz = z, maxz = z;
	for (int vi : VertexIndicesItr()) 
	{
		int k = 3 * vi;
		x = Vertices[k]; y = Vertices[k + 1]; z = Vertices[k + 2];
		if (x < minx) minx = x; else if (x > maxx) maxx = x;
		if (y < miny) miny = y; else if (y > maxy) maxy = y;
		if (z < minz) minz = z; else if (z > maxz) maxz = z;
	}
	return FAxisAlignedBox3d(FVector3d(minx, miny, minz), FVector3d(maxx, maxy, maxz));
}


FAxisAlignedBox3d FDynamicMesh3::GetCachedBounds()
{
	if (CachedBoundingBoxTimestamp != GetShapeTimestamp()) 
	{
		CachedBoundingBox = GetBounds();
		CachedBoundingBoxTimestamp = GetShapeTimestamp();
	}
	return CachedBoundingBox;
}




bool FDynamicMesh3::IsClosed() const 
{
	if (TriangleCount() == 0) 
	{
		return false;
	}

	int N = MaxEdgeID();
	for (int i = 0; i < N; ++i)
	{
		if (EdgeRefCounts.IsValid(i) && IsBoundaryEdge(i))
		{
			return false;
		}
	}
	return true;
}


bool FDynamicMesh3::GetCachedIsClosed() 
{
	if (CachedIsClosedTimestamp != GetTopologyTimestamp()) 
	{
		bIsClosedCached = IsClosed();
		CachedIsClosedTimestamp = GetTopologyTimestamp();
	}
	return bIsClosedCached;
}





// average of 1 or 2 face normals
FVector3d FDynamicMesh3::GetEdgeNormal(int eID) const
{
	if (EdgeRefCounts.IsValid(eID)) 
	{
		int ei = 4 * eID;
		FVector3d n = GetTriNormal(Edges[ei + 2]);
		if (Edges[ei + 3] != InvalidID) 
		{
			n += GetTriNormal(Edges[ei + 3]);
			n.Normalize();
		}
		return n;
	}
	check(false);
	return FVector3d::Zero();
}

FVector3d FDynamicMesh3::GetEdgePoint(int eID, double t) const
{
	t = VectorUtil::Clamp(t, 0.0, 1.0);
	if (EdgeRefCounts.IsValid(eID)) 
	{
		int ei = 4 * eID;
		int iv0 = 3 * Edges[ei];
		int iv1 = 3 * Edges[ei + 1];
		double mt = 1.0 - t;
		return FVector3d(
			mt*Vertices[iv0] + t * Vertices[iv1],
			mt*Vertices[iv0 + 1] + t * Vertices[iv1 + 1],
			mt*Vertices[iv0 + 2] + t * Vertices[iv1 + 2]);
	}
	check(false);
	return FVector3d::Zero();
}



void FDynamicMesh3::GetVtxOneRingCentroid(int vID, FVector3d& centroid) const
{
	centroid = FVector3d::Zero();
	if (VertexRefCounts.IsValid(vID)) 
	{
		int n = 0;
		for (int eid : VertexEdgeLists.Values(vID)) 
		{
			int other_idx = 3 * GetOtherEdgeVertex(eid, vID);
			centroid.X += Vertices[other_idx];
			centroid.Y += Vertices[other_idx + 1];
			centroid.Z += Vertices[other_idx + 2];
			n++;
		}
		if (n > 0) 
		{
			centroid *= 1.0 / n;
		}
	}
}



FFrame3d FDynamicMesh3::GetVertexFrame(int vID, bool bFrameNormalY) const
{
	check(HasVertexNormals());

	int vi = 3 * vID;
	FVector3d v(Vertices[vi], Vertices[vi + 1], Vertices[vi + 2]);
	FVector3d normal((*VertexNormals)[vi], (*VertexNormals)[vi + 1], (*VertexNormals)[vi + 2]);
	int eid = VertexEdgeLists.First(vID);
	int ovi = 3 * GetOtherEdgeVertex(eid, vID);
	FVector3d ov(Vertices[ovi], Vertices[ovi + 1], Vertices[ovi + 2]);
	FVector3d edge = (ov - v);
	edge.Normalize();

	FVector3d other = normal.Cross(edge);
	edge = other.Cross(normal);
	if (bFrameNormalY)
	{
		return FFrame3d(v, edge, normal, -other);
	}
	else
	{
		return FFrame3d(v, edge, other, normal);
	}
}



FVector3d FDynamicMesh3::GetTriNormal(int tID) const
{
	FVector3d v0, v1, v2;
	GetTriVertices(tID, v0, v1, v2);
	return VectorUtil::Normal(v0, v1, v2);
}

double FDynamicMesh3::GetTriArea(int tID) const
{
	FVector3d v0, v1, v2;
	GetTriVertices(tID, v0, v1, v2);
	return VectorUtil::Area(v0, v1, v2);
}



void FDynamicMesh3::GetTriInfo(int tID, FVector3d& Normal, double& Area, FVector3d& Centroid) const
{
	FVector3d v0, v1, v2;
	GetTriVertices(tID, v0, v1, v2);
	Centroid = (v0 + v1 + v2) * (1.0 / 3.0);
	Area = VectorUtil::Area(v0, v1, v2);
	Normal = VectorUtil::Normal(v0, v1, v2);
	//normal = FastNormalArea(ref v0, ref v1, ref v2, out fArea);
}


FVector3d FDynamicMesh3::GetTriBaryPoint(int tID, double bary0, double bary1, double bary2) const 
{
	int ai = 3 * Triangles[3 * tID],
		bi = 3 * Triangles[3 * tID + 1],
		ci = 3 * Triangles[3 * tID + 2];
	return FVector3d(
		(bary0*Vertices[ai] + bary1 * Vertices[bi] + bary2 * Vertices[ci]),
		(bary0*Vertices[ai + 1] + bary1 * Vertices[bi + 1] + bary2 * Vertices[ci + 1]),
		(bary0*Vertices[ai + 2] + bary1 * Vertices[bi + 2] + bary2 * Vertices[ci + 2]));
}


FVector3d FDynamicMesh3::GetTriBaryNormal(int tID, double bary0, double bary1, double bary2) const
{
	check(HasVertexNormals());
	int ai = 3 * Triangles[3 * tID],
		bi = 3 * Triangles[3 * tID + 1],
		ci = 3 * Triangles[3 * tID + 2];
	const TDynamicVector<float>& normalsR = *(this->VertexNormals);
	FVector3d n = FVector3d(
		(bary0*normalsR[ai] + bary1 * normalsR[bi] + bary2 * normalsR[ci]),
		(bary0*normalsR[ai + 1] + bary1 * normalsR[bi + 1] + bary2 * normalsR[ci + 1]),
		(bary0*normalsR[ai + 2] + bary1 * normalsR[bi + 2] + bary2 * normalsR[ci + 2]));
	n.Normalize();
	return n;
}

FVector3d FDynamicMesh3::GetTriCentroid(int tID) const
{
	int ai = 3 * Triangles[3 * tID],
		bi = 3 * Triangles[3 * tID + 1],
		ci = 3 * Triangles[3 * tID + 2];
	double f = (1.0 / 3.0);
	return FVector3d(
		(Vertices[ai] + Vertices[bi] + Vertices[ci]) * f,
		(Vertices[ai + 1] + Vertices[bi + 1] + Vertices[ci + 1]) * f,
		(Vertices[ai + 2] + Vertices[bi + 2] + Vertices[ci + 2]) * f);
}


void FDynamicMesh3::GetTriBaryPoint(int tID, double bary0, double bary1, double bary2, FVertexInfo& vinfo) const
{
	vinfo = FVertexInfo();
	int ai = 3 * Triangles[3 * tID],
		bi = 3 * Triangles[3 * tID + 1],
		ci = 3 * Triangles[3 * tID + 2];
	vinfo.Position = FVector3d(
		(bary0 * Vertices[ai] + bary1 * Vertices[bi] + bary2 * Vertices[ci]),
		(bary0 * Vertices[ai + 1] + bary1 * Vertices[bi + 1] + bary2 * Vertices[ci + 1]),
		(bary0 * Vertices[ai + 2] + bary1 * Vertices[bi + 2] + bary2 * Vertices[ci + 2]));
	vinfo.bHaveN = HasVertexNormals();
	if (vinfo.bHaveN) 
	{
		TDynamicVector<float>& normalsR = *(this->VertexNormals);
		vinfo.Normal = FVector3f(
			(float)(bary0 * normalsR[ai] + bary1 * normalsR[bi] + bary2 * normalsR[ci]),
			(float)(bary0 * normalsR[ai + 1] + bary1 * normalsR[bi + 1] + bary2 * normalsR[ci + 1]),
			(float)(bary0 * normalsR[ai + 2] + bary1 * normalsR[bi + 2] + bary2 * normalsR[ci + 2]));
		vinfo.Normal.Normalize();
	}
	vinfo.bHaveC = HasVertexColors();
	if (vinfo.bHaveC) 
	{
		TDynamicVector<float>& colorsR = *(this->VertexColors);
		vinfo.Color = FVector3f(
			(float)(bary0 * colorsR[ai] + bary1 * colorsR[bi] + bary2 * colorsR[ci]),
			(float)(bary0 * colorsR[ai + 1] + bary1 * colorsR[bi + 1] + bary2 * colorsR[ci + 1]),
			(float)(bary0 * colorsR[ai + 2] + bary1 * colorsR[bi + 2] + bary2 * colorsR[ci + 2]));
	}
	vinfo.bHaveUV = HasVertexUVs();
	if (vinfo.bHaveUV) 
	{
		TDynamicVector<float>& uvR = *(this->VertexUVs);
		ai = 2 * Triangles[3 * tID];
		bi = 2 * Triangles[3 * tID + 1];
		ci = 2 * Triangles[3 * tID + 2];
		vinfo.UV = FVector2f(
			(float)(bary0 * uvR[ai] + bary1 * uvR[bi] + bary2 * uvR[ci]),
			(float)(bary0 * uvR[ai + 1] + bary1 * uvR[bi + 1] + bary2 * uvR[ci + 1]));
	}
}


FAxisAlignedBox3d FDynamicMesh3::GetTriBounds(int tID) const
{
	int vi = 3 * Triangles[3 * tID];
	double x = Vertices[vi], y = Vertices[vi + 1], z = Vertices[vi + 2];
	double minx = x, maxx = x, miny = y, maxy = y, minz = z, maxz = z;
	for (int i = 1; i < 3; ++i) 
	{
		vi = 3 * Triangles[3 * tID + i];
		x = Vertices[vi]; y = Vertices[vi + 1]; z = Vertices[vi + 2];
		if (x < minx) minx = x; else if (x > maxx) maxx = x;
		if (y < miny) miny = y; else if (y > maxy) maxy = y;
		if (z < minz) minz = z; else if (z > maxz) maxz = z;
	}
	return FAxisAlignedBox3d(FVector3d(minx, miny, minz), FVector3d(maxx, maxy, maxz));
}


FFrame3d FDynamicMesh3::GetTriFrame(int tID, int nEdge) const
{
	int ti = 3 * tID;
	int a = 3 * Triangles[ti + (nEdge % 3)];
	int b = 3 * Triangles[ti + ((nEdge + 1) % 3)];
	int c = 3 * Triangles[ti + ((nEdge + 2) % 3)];
	FVector3d v1(Vertices[a], Vertices[a + 1], Vertices[a + 2]);
	FVector3d v2(Vertices[b], Vertices[b + 1], Vertices[b + 2]);
	FVector3d v3(Vertices[c], Vertices[c + 1], Vertices[c + 2]);

	FVector3d edge1 = v2 - v1;  edge1.Normalize();
	FVector3d edge2 = v3 - v2;  edge2.Normalize();
	FVector3d normal = edge2.Cross(edge1); normal.Normalize();

	FVector3d other = normal.Cross(edge1);

	FVector3d center = (v1 + v2 + v3) / 3;
	return FFrame3d(center, edge1, other, normal);
}



double FDynamicMesh3::GetTriSolidAngle(int tID, const FVector3d& p) const
{
	// inlined version of GetTriVertices & VectorUtil::TriSolidAngle
	int ti = 3 * tID;
	int ta = 3 * Triangles[ti];
	FVector3d a(Vertices[ta] - p.X, Vertices[ta + 1] - p.Y, Vertices[ta + 2] - p.Z);
	int tb = 3 * Triangles[ti + 1];
	FVector3d b(Vertices[tb] - p.X, Vertices[tb + 1] - p.Y, Vertices[tb + 2] - p.Z);
	int tc = 3 * Triangles[ti + 2];
	FVector3d c(Vertices[tc] - p.X, Vertices[tc + 1] - p.Y, Vertices[tc + 2] - p.Z);
	double la = a.Length(), lb = b.Length(), lc = c.Length();
	double top = (la * lb * lc) + a.Dot(b) * lc + b.Dot(c) * la + c.Dot(a) * lb;
	double bottom = a.X * (b.Y * c.Z - c.Y * b.Z) - a.Y * (b.X * c.Z - c.X * b.Z) + a.Z * (b.X * c.Y - c.X * b.Y);
	// -2 instead of 2 to account for UE winding
	return -2.0 * atan2(bottom, top);
}



double FDynamicMesh3::GetTriInternalAngleR(int tID, int i)
{
	int ti = 3 * tID;
	int ta = 3 * Triangles[ti];
	FVector3d a(Vertices[ta], Vertices[ta + 1], Vertices[ta + 2]);
	int tb = 3 * Triangles[ti + 1];
	FVector3d b(Vertices[tb], Vertices[tb + 1], Vertices[tb + 2]);
	int tc = 3 * Triangles[ti + 2];
	FVector3d c(Vertices[tc], Vertices[tc + 1], Vertices[tc + 2]);
	if (i == 0)
	{
		return (b - a).Normalized().AngleR((c - a).Normalized());
	}
	else if (i == 1)
	{
		return (a - b).Normalized().AngleR((c - b).Normalized());
	}
	else
	{
		return (a - c).Normalized().AngleR((b - c).Normalized());
	}
}


double FDynamicMesh3::CalculateWindingNumber(const FVector3d& QueryPoint) const
{
	double sum = 0;
	for (int tid : TriangleIndicesItr())
	{
		sum += GetTriSolidAngle(tid, QueryPoint);
	}
	return sum / FMathd::FourPi;
}