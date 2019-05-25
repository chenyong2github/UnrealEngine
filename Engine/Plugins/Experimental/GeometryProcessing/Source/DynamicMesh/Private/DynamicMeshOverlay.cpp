// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshOverlay.h"
#include "DynamicMesh3.h"




template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::ClearElements()
{
	Elements.Clear();
	ElementsRefCounts = FRefCountVector();
	ParentVertices.Clear();
	InitializeTriangles(ParentMesh->MaxTriangleID());
}



template<typename RealType, int ElementSize>
int TDynamicMeshOverlay<RealType, ElementSize>::AppendElement(RealType ConstantValue, int SourceVertex)
{
	int vid = ElementsRefCounts.Allocate();
	int i = ElementSize * vid;
	for (int k = ElementSize - 1; k >= 0; --k)
	{
		Elements.InsertAt(ConstantValue, i + k);
	}
	ParentVertices.InsertAt(SourceVertex, vid);

	//updateTimeStamp(true);
	return vid;
}


template<typename RealType, int ElementSize>
int TDynamicMeshOverlay<RealType, ElementSize>::AppendElement(const RealType* Value, int SourceVertex)
{
	int vid = ElementsRefCounts.Allocate();
	int i = ElementSize * vid;

	// insert in reverse order so that Resize() is only called once
	for (int k = ElementSize - 1; k >= 0; --k)
	{
		Elements.InsertAt(Value[k], i + k);
	}

	ParentVertices.InsertAt(SourceVertex, vid);

	//updateTimeStamp(true);
	return vid;
}




template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::InitializeTriangles(int MaxTriangleID)
{
	ElementTriangles.Resize(0);
	ElementTriangles.Resize(MaxTriangleID * 3, FDynamicMesh3::InvalidID);
}




template<typename RealType, int ElementSize>
EMeshResult TDynamicMeshOverlay<RealType, ElementSize>::SetTriangle(int tid, const FIndex3i& tv)
{
	if (IsElement(tv[0]) == false || IsElement(tv[1]) == false || IsElement(tv[2]) == false)
	{
		check(false);
		return EMeshResult::Failed_NotAVertex;
	}
	if (tv[0] == tv[1] || tv[0] == tv[2] || tv[1] == tv[2]) 
	{
		check(false);
		return EMeshResult::Failed_InvalidNeighbourhood;
	}

	InternalSetTriangle(tid, tv, true);

	//updateTimeStamp(true);
	return EMeshResult::Ok;
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::InternalSetTriangle(int tid, const FIndex3i& tv, bool bIncrementRefCounts)
{
	int i = 3 * tid;
	ElementTriangles.InsertAt(tv[2], i + 2);
	ElementTriangles.InsertAt(tv[1], i + 1);
	ElementTriangles.InsertAt(tv[0], i);

	if (bIncrementRefCounts)
	{
		ElementsRefCounts.Increment(tv[0]);
		ElementsRefCounts.Increment(tv[1]);
		ElementsRefCounts.Increment(tv[2]);
	}
}



template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::InitializeNewTriangle(int tid)
{
	int i = 3 * tid;
	ElementTriangles.InsertAt(FDynamicMesh3::InvalidID, i + 2);
	ElementTriangles.InsertAt(FDynamicMesh3::InvalidID, i + 1);
	ElementTriangles.InsertAt(FDynamicMesh3::InvalidID, i);

	//updateTimeStamp(true);
}




template<typename RealType, int ElementSize>
bool TDynamicMeshOverlay<RealType,ElementSize>::IsSeamEdge(int eid) const
{
	FIndex2i et = ParentMesh->GetEdgeT(eid);
	if (et.B == FDynamicMesh3::InvalidID)
	{
		return true;
	}

	FIndex2i ev = ParentMesh->GetEdgeV(eid);
	int base_a = ev.A, base_b = ev.B;

	FIndex3i Triangle0 = GetTriangle(et.A);
	FIndex3i BaseTriangle0(ParentVertices[Triangle0.A], ParentVertices[Triangle0.B], ParentVertices[Triangle0.C]);
	int idx_base_a1 = BaseTriangle0.IndexOf(base_a);
	int idx_base_b1 = BaseTriangle0.IndexOf(base_b);

	FIndex3i Triangle1 = GetTriangle(et.B);
	FIndex3i BaseTriangle1(ParentVertices[Triangle1.A], ParentVertices[Triangle1.B], ParentVertices[Triangle1.C]);
	int idx_base_a2 = BaseTriangle1.IndexOf(base_a);
	int idx_base_b2 = BaseTriangle1.IndexOf(base_b);

	return !IndexUtil::SamePairUnordered(Triangle0[idx_base_a1], Triangle0[idx_base_b1], Triangle1[idx_base_a2], Triangle1[idx_base_b2]);


	// [RMS] this doesn't seem to work but it should, and would be more efficient. 
	//   - add ParentMesh->FindTriEdgeIndex(tid,eid)
	//   - SamePairUnordered query could directly index into ElementTriangles[]
	//FIndex3i TriangleA = GetTriangle(et.A);
	//FIndex3i TriangleB = GetTriangle(et.B);

	//FIndex3i BaseTriEdgesA = ParentMesh->GetTriEdges(et.A);
	//int WhichA = (BaseTriEdgesA.A == eid) ? 0 :
	//	((BaseTriEdgesA.B == eid) ? 1 : 2);

	//FIndex3i BaseTriEdgesB = ParentMesh->GetTriEdges(et.B);
	//int WhichB = (BaseTriEdgesB.A == eid) ? 0 :
	//	((BaseTriEdgesB.B == eid) ? 1 : 2);

	//return SamePairUnordered(
	//	TriangleA[WhichA], TriangleA[(WhichA + 1) % 3],
	//	TriangleB[WhichB], TriangleB[(WhichB + 1) % 3]);
}



template<typename RealType, int ElementSize>
bool TDynamicMeshOverlay<RealType, ElementSize>::IsSeamVertex(int vid) const
{
	// @todo can we do this more efficiently? At minimum we are looking up each triangle twice...
	for (int edgeid : ParentMesh->VtxEdgesItr(vid))
	{
		if (IsSeamEdge(edgeid))
		{
			return true;
		}
	}
	return false;
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::GetVertexElements(int vid, TArray<int>& OutElements) const
{
	OutElements.Reset();
	for (int tid : ParentMesh->VtxTrianglesItr(vid))
	{
		FIndex3i Triangle = GetTriangle(tid);
		for (int j = 0; j < 3; ++j)
		{
			if (ParentVertices[Triangle[j]] == vid)
			{
				OutElements.AddUnique(Triangle[j]);
			}
		}
	}
}



template<typename RealType, int ElementSize>
int TDynamicMeshOverlay<RealType, ElementSize>::CountVertexElements(int vid, bool bBruteForce) const
{
	TArray<int> VertexElements;
	if (bBruteForce) 
	{
		for (int tid : ParentMesh->TriangleIndicesItr())
		{
			FIndex3i Triangle = GetTriangle(tid);
			for (int j = 0; j < 3; ++j)
			{
				if (ParentVertices[Triangle[j]] == vid)
				{
					VertexElements.AddUnique(Triangle[j]);
				}
			}
		}
	}
	else
	{
		for (int tid : ParentMesh->VtxTrianglesItr(vid))
		{
			FIndex3i Triangle = GetTriangle(tid);
			for (int j = 0; j < 3; ++j)
			{
				if (ParentVertices[Triangle[j]] == vid)
				{
					VertexElements.AddUnique(Triangle[j]);
				}
			}
		}
	}

	return VertexElements.Num();
}




template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnRemoveTriangle(int TriangleID, bool bRemoveIsolatedVertices)
{
	FIndex3i Triangle = GetTriangle(TriangleID);
	InitializeNewTriangle(TriangleID);

	// decrement element refcounts, and free element if it is now unreferenced
	for (int j = 0; j < 3; ++j) 
	{
		int elemid = Triangle[j];
		ElementsRefCounts.Decrement(elemid);
		if (bRemoveIsolatedVertices && ElementsRefCounts.GetRefCount(elemid) == 1) 
		{
			ElementsRefCounts.Decrement(elemid);
			ParentVertices[elemid] = FDynamicMesh3::InvalidID;
			check(ElementsRefCounts.IsValid(elemid) == false);
		}
	}
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnReverseTriOrientation(int TriangleID)
{
	FIndex3i Triangle = GetTriangle(TriangleID);
	int i = 3 * TriangleID;
	ElementTriangles[i] = Triangle[1];			// mirrors order in FDynamicMesh3::ReverseTriOrientationInternal
	ElementTriangles[i + 1] = Triangle[0];
	ElementTriangles[i + 2] = Triangle[2];
}



//#pragma optimize( "", off )
template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& splitInfo)
{
	int orig_t0 = splitInfo.OriginalTriangles.A;
	int orig_t1 = splitInfo.OriginalTriangles.B;
	int base_a = splitInfo.OriginalVertices.A;
	int base_b = splitInfo.OriginalVertices.B;

	// look up current triangle 0, and infer base triangle 0
	// @todo handle case where these are InvalidID because no UVs exist for this triangle
	FIndex3i Triangle0 = GetTriangle(orig_t0);
	FIndex3i BaseTriangle0(ParentVertices[Triangle0.A], ParentVertices[Triangle0.B], ParentVertices[Triangle0.C]);
	int idx_base_a1 = BaseTriangle0.IndexOf(base_a);
	int idx_base_b1 = BaseTriangle0.IndexOf(base_b);
	int idx_base_c = IndexUtil::GetOtherTriIndex(idx_base_a1, idx_base_b1);

	// create new element at lerp position
	int NewElemID = AppendElement((RealType)0, splitInfo.NewVertex);
	SetElementFromLerp(NewElemID, Triangle0[idx_base_a1], Triangle0[idx_base_b1], (RealType)splitInfo.SplitT);

	// rewrite triangle 0
	ElementTriangles[3*orig_t0 + idx_base_b1] = NewElemID;

	// create new triangle 2 w/ correct winding order
	FIndex3i NewTriangle2(NewElemID, Triangle0[idx_base_b1], Triangle0[idx_base_c]);  // mirrors DMesh3::SplitEdge [f,b,c]
	InternalSetTriangle(splitInfo.NewTriangles.A, NewTriangle2, false);

	// update ref counts
	ElementsRefCounts.Increment(NewElemID, 2);
	ElementsRefCounts.Increment(Triangle0[idx_base_c]);

	if (orig_t1 == FDynamicMesh3::InvalidID)
	{
		return;  // we are done if this is a boundary triangle
	}

	// look up current triangle1 and infer base triangle 1
	// @todo handle case where these are InvalidID because no UVs exist for this triangle
	FIndex3i Triangle1 = GetTriangle(orig_t1);
	FIndex3i BaseTriangle1(ParentVertices[Triangle1.A], ParentVertices[Triangle1.B], ParentVertices[Triangle1.C]);
	int idx_base_a2 = BaseTriangle1.IndexOf(base_a);
	int idx_base_b2 = BaseTriangle1.IndexOf(base_b);
	int idx_base_d = IndexUtil::GetOtherTriIndex(idx_base_a2, idx_base_b2);

	int OtherNewElemID = NewElemID;

	// if we don't have a shared edge, we need to create another new UV for the other side
	bool bHasSharedUVEdge = IndexUtil::SamePairUnordered(Triangle0[idx_base_a1], Triangle0[idx_base_b1], Triangle1[idx_base_a2], Triangle1[idx_base_b2]);
	if (bHasSharedUVEdge == false)
	{
		// create new element at lerp position
		OtherNewElemID = AppendElement((RealType)0, splitInfo.NewVertex);
		SetElementFromLerp(OtherNewElemID, Triangle1[idx_base_a2], Triangle1[idx_base_b2], (RealType)splitInfo.SplitT);
	}

	// rewrite triangle 1
	ElementTriangles[3*orig_t1 + idx_base_b2] = OtherNewElemID;

	// create new triangle 3 w/ correct winding order
	FIndex3i NewTriangle3(OtherNewElemID, Triangle1[idx_base_d], Triangle1[idx_base_b2]);  // mirrors DMesh3::SplitEdge [f,d,b]
	InternalSetTriangle(splitInfo.NewTriangles.B, NewTriangle3, false);

	// update ref counts
	ElementsRefCounts.Increment(OtherNewElemID, 2);
	ElementsRefCounts.Increment(Triangle1[idx_base_d]);
}



//#pragma optimize("", off)
template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo& FlipInfo)
{
	int orig_t0 = FlipInfo.Triangles.A;
	int orig_t1 = FlipInfo.Triangles.B;
	int base_a = FlipInfo.OriginalVerts.A;
	int base_b = FlipInfo.OriginalVerts.B;
	int base_c = FlipInfo.OpposingVerts.A;
	int base_d = FlipInfo.OpposingVerts.B;

	// look up triangle 0
	FIndex3i Triangle0 = GetTriangle(orig_t0);
	FIndex3i BaseTriangle0(ParentVertices[Triangle0.A], ParentVertices[Triangle0.B], ParentVertices[Triangle0.C]);
	int idx_base_a1 = BaseTriangle0.IndexOf(base_a);
	int idx_base_b1 = BaseTriangle0.IndexOf(base_b);
	int idx_base_c = IndexUtil::GetOtherTriIndex(idx_base_a1, idx_base_b1);

	// look up triangle 1 (must exist because base mesh would never flip a boundary edge)
	FIndex3i Triangle1 = GetTriangle(orig_t1);
	FIndex3i BaseTriangle1(ParentVertices[Triangle1.A], ParentVertices[Triangle1.B], ParentVertices[Triangle1.C]);
	int idx_base_a2 = BaseTriangle1.IndexOf(base_a);
	int idx_base_b2 = BaseTriangle1.IndexOf(base_b);
	int idx_base_d = IndexUtil::GetOtherTriIndex(idx_base_a2, idx_base_b2);

	// sanity checks
	check(idx_base_c == BaseTriangle0.IndexOf(base_c));
	check(idx_base_d == BaseTriangle1.IndexOf(base_d));

	// we should not have been called on a non-shared edge!!
	bool bHasSharedUVEdge = IndexUtil::SamePairUnordered(Triangle0[idx_base_a1], Triangle0[idx_base_b1], Triangle1[idx_base_a2], Triangle1[idx_base_b2]);
	check(bHasSharedUVEdge);

	int A = Triangle0[idx_base_a1];
	int B = Triangle0[idx_base_b1];
	int C = Triangle0[idx_base_c];
	int D = Triangle1[idx_base_d];

	// set triangles to same index order as in FDynamicMesh::FlipEdge
	int i0 = 3 * orig_t0;
	ElementTriangles[i0] = C; ElementTriangles[i0+1] = D; ElementTriangles[i0+2] = B;
	int i1 = 3 * orig_t1;
	ElementTriangles[i1] = D; ElementTriangles[i1+1] = C; ElementTriangles[i1+2] = A;

	// update reference counts
	ElementsRefCounts.Decrement(A);
	ElementsRefCounts.Decrement(B);
	ElementsRefCounts.Increment(C);
	ElementsRefCounts.Increment(D);
}





//#pragma optimize("", off)
template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo& collapseInfo)
{
	int vid_base_kept = collapseInfo.KeptVertex;
	int vid_base_removed = collapseInfo.RemovedVertex;
	int tid_removed0 = collapseInfo.RemovedTris.A;
	int tid_removed1 = collapseInfo.RemovedTris.B;

	// look up triangle 0
	FIndex3i Triangle0 = GetTriangle(tid_removed0);
	FIndex3i BaseTriangle0(ParentVertices[Triangle0.A], ParentVertices[Triangle0.B], ParentVertices[Triangle0.C]);
	int idx_removed0_a = BaseTriangle0.IndexOf(vid_base_kept);
	int idx_removed0_b = BaseTriangle0.IndexOf(vid_base_removed);

	// look up triangle 1 if this is not a boundary edge
	FIndex3i Triangle1, BaseTriangle1;
	if (collapseInfo.bIsBoundary == false)
	{
		Triangle1 = GetTriangle(tid_removed1);
		BaseTriangle1 = FIndex3i(ParentVertices[Triangle1.A], ParentVertices[Triangle1.B], ParentVertices[Triangle1.C]);

		int idx_removed1_a = BaseTriangle1.IndexOf(vid_base_kept);
		int idx_removed1_b = BaseTriangle1.IndexOf(vid_base_removed);

		// if this is an internal edge it cannot be a seam or we cannot collapse
		bool bHasSharedUVEdge = IndexUtil::SamePairUnordered(Triangle0[idx_removed0_a], Triangle0[idx_removed0_b], Triangle1[idx_removed1_a], Triangle1[idx_removed1_b]);
		check(bHasSharedUVEdge);
	}

	// need to find the elementid for the "kept" vertex. Since this isn't a seam, 
	// there is just one, and we can grab it from "old" removed triangle
	int kept_elemid = FDynamicMesh3::InvalidID;
	for (int j = 0; j < 3; ++j) 
	{
		if (BaseTriangle0[j] == vid_base_kept)
		{
			kept_elemid = Triangle0[j];
		}
	}
	check(kept_elemid != FDynamicMesh3::InvalidID);

	// look for still-existing triangles that have elements linked to the removed vertex.
	// in that case, replace with the element we found
	TArray<int> removed_elements;
	for (int onering_tid : ParentMesh->VtxTrianglesItr(vid_base_kept))
	{
		FIndex3i elem_tri = GetTriangle(onering_tid);
		for (int j = 0; j < 3; ++j) 
		{
			int elem_id = elem_tri[j];
			if (ParentVertices[elem_id] == vid_base_removed) 
			{
				ElementsRefCounts.Decrement(elem_id);
				removed_elements.AddUnique(elem_id);
				ElementTriangles[3*onering_tid + j] = kept_elemid;
				ElementsRefCounts.Increment(kept_elemid);
			}
		}
	}
	check(removed_elements.Num() == 1);   // should always be true for non-seam edges...

	// update position of kept element
	SetElementFromLerp(kept_elemid, kept_elemid, removed_elements[0], collapseInfo.CollapseT);

	// clear the two triangles we removed
	for (int j = 0; j < 3; ++j) 
	{
		ElementsRefCounts.Decrement(Triangle0[j]);
		ElementTriangles[3*tid_removed0 + j] = FDynamicMesh3::InvalidID;
		if (collapseInfo.bIsBoundary == false) 
		{
			ElementsRefCounts.Decrement(Triangle1[j]);
			ElementTriangles[3*tid_removed1 + j] = FDynamicMesh3::InvalidID;
		}
	}

	// remove the elements associated with the removed vertex
	for (int k = 0; k < removed_elements.Num(); ++k) 
	{
		check(ElementsRefCounts.GetRefCount(removed_elements[k]) == 1);
		ElementsRefCounts.Decrement(removed_elements[k]);
		ParentVertices[removed_elements[k]] = FDynamicMesh3::InvalidID;
	}

}



template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo& PokeInfo)
{
	FIndex3i Triangle = GetTriangle(PokeInfo.OriginalTriangle);

	// create new element at barycentric position
	int CenterElemID = AppendElement((RealType)0, PokeInfo.NewVertex);
	FVector3<RealType> BaryCoords((RealType)PokeInfo.BaryCoords.X, (RealType)PokeInfo.BaryCoords.Y, (RealType)PokeInfo.BaryCoords.Z);
	SetElementFromBary(CenterElemID, Triangle[0], Triangle[1], Triangle[2], BaryCoords);

	// update orig triangle and two new ones. Winding orders here mirror FDynamicMesh3::PokeTriangle
	SetTriangle(PokeInfo.OriginalTriangle, FIndex3i(Triangle[0], Triangle[1], CenterElemID) );
	SetTriangle(PokeInfo.NewTriangles.A, FIndex3i(Triangle[1], Triangle[2], CenterElemID));
	SetTriangle(PokeInfo.NewTriangles.B, FIndex3i(Triangle[2], Triangle[0], CenterElemID));

	ElementsRefCounts.Increment(Triangle[0]);
	ElementsRefCounts.Increment(Triangle[1]);
	ElementsRefCounts.Increment(Triangle[2]);
	ElementsRefCounts.Increment(CenterElemID, 3);
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnMergeEdges(const FDynamicMesh3::FMergeEdgesInfo& MergeInfo)
{
	// MergeEdges just merges vertices. For now we will not also merge UVs. So all we need to
	// do is rewrite the UV parent vertices

	FIndex3i ModifiedEdges(MergeInfo.KeptEdge, MergeInfo.ExtraKeptEdges.A, MergeInfo.ExtraKeptEdges.B);
	for (int EdgeIdx = 0; EdgeIdx < 3; ++EdgeIdx)
	{
		if (ParentMesh->IsEdge(ModifiedEdges[EdgeIdx]) == false)
		{
			continue;
		}

		FIndex2i EdgeTris = ParentMesh->GetEdgeT(ModifiedEdges[EdgeIdx]);
		for (int j = 0; j < 2; ++j)
		{
			FIndex3i ElemTriangle = GetTriangle(EdgeTris[j]);
			for (int k = 0; k < 3; ++k)
			{
				int ParentVID = ParentVertices[ElemTriangle[k]];
				if (ParentVID == MergeInfo.RemovedVerts.A)
				{
					ParentVertices[ElemTriangle[k]] = MergeInfo.KeptVerts.A;
				}
				else if (ParentVID == MergeInfo.RemovedVerts.B)
				{
					ParentVertices[ElemTriangle[k]] = MergeInfo.KeptVerts.B;
				}
			}
		}
	}
}




template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::SetElementFromLerp(int SetElement, int ElementA, int ElementB, RealType Alpha)
{
	int IndexSet = ElementSize * SetElement;
	int IndexA = ElementSize * ElementA;
	int IndexB = ElementSize * ElementB;
	RealType Beta = ((RealType)1 - Alpha);
	for (int i = 0; i < ElementSize; ++i)
	{
		Elements[IndexSet+i] = Beta*Elements[IndexA+i] + Alpha*Elements[IndexB+i];
	}
}

template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::SetElementFromBary(int SetElement, int ElementA, int ElementB, int ElementC, const FVector3<RealType>& BaryCoords)
{
	int IndexSet = ElementSize * SetElement;
	int IndexA = ElementSize * ElementA;
	int IndexB = ElementSize * ElementB;
	int IndexC = ElementSize * ElementC;
	for (int i = 0; i < ElementSize; ++i)
	{
		Elements[IndexSet + i] = 
			BaryCoords.X*Elements[IndexA+i] + BaryCoords.Y*Elements[IndexB+i] + BaryCoords.Z*Elements[IndexC+i];
	}
}


//#pragma optimize("", off)
template<typename RealType, int ElementSize>
bool TDynamicMeshOverlay<RealType, ElementSize>::CheckValidity(bool bAllowNonManifoldVertices, EValidityCheckFailMode FailMode) const
{
	bool is_ok = true;
	TFunction<void(bool)> CheckOrFailF = [&](bool b)
	{
		is_ok = is_ok && b;
	};
	if (FailMode == EValidityCheckFailMode::Check)
	{
		CheckOrFailF = [&](bool b)
		{
			checkf(b, TEXT("TDynamicMeshOverlay::CheckValidity failed!"));
			is_ok = is_ok && b;
		};
	}
	else if (FailMode == EValidityCheckFailMode::Ensure)
	{
		CheckOrFailF = [&](bool b)
		{
			ensureMsgf(b, TEXT("TDynamicMeshOverlay::CheckValidity failed!"));
			is_ok = is_ok && b;
		};
	}


	// @todo: check that all connected element-pairs are also edges in parentmesh

	// check that parent vtx of each element is actually a vertex
	for (int elemid : ElementIndicesItr())
	{
		int ParentVID = GetParentVertex(elemid);
		CheckOrFailF(ParentMesh->IsVertex(ParentVID));
	}

	// check that parent vertices of each element triangle are the same as base triangle
	for (int tid : ParentMesh->TriangleIndicesItr())
	{
		FIndex3i ElemTri = GetTriangle(tid);
		FIndex3i BaseTri = ParentMesh->GetTriangle(tid);
		for (int j = 0; j < 3; ++j)
		{
			if (ElemTri[j] != FDynamicMesh3::InvalidID)
			{
				CheckOrFailF(GetParentVertex(ElemTri[j]) == BaseTri[j]);
			}
		}
	}

	// count references to each element
	TArray<int> RealRefCounts; RealRefCounts.Init(0, MaxElementID());
	for (int tid : ParentMesh->TriangleIndicesItr())
	{
		FIndex3i Tri = GetTriangle(tid);
		for (int j = 0; j < 3; ++j)
		{
			if (Tri[j] != FDynamicMesh3::InvalidID)
			{
				RealRefCounts[Tri[j]] += 1;
			}
		}
	}
	// verify that refcount list counts are same as actual reference counts
	for (int elemid : ElementsRefCounts.Indices())
	{
		int CurRefCount = ElementsRefCounts.GetRefCount(elemid);
		CheckOrFailF(CurRefCount == RealRefCounts[elemid] + 1);
	}

	return is_ok;
}





// These are explicit instantiations of the templates that are exported from the shared lib.
// Only these instantiations of the template can be used.
// This is necessary because we have placed most of the templated functions in this .cpp file, instead of the header.
template class DYNAMICMESH_API TDynamicMeshOverlay<float, 1>;
template class DYNAMICMESH_API TDynamicMeshOverlay<double, 1>;
template class DYNAMICMESH_API TDynamicMeshOverlay<int, 1>;
template class DYNAMICMESH_API TDynamicMeshOverlay<float, 2>;
template class DYNAMICMESH_API TDynamicMeshOverlay<double, 2>;
template class DYNAMICMESH_API TDynamicMeshOverlay<int, 2>;
template class DYNAMICMESH_API TDynamicMeshOverlay<float, 3>;
template class DYNAMICMESH_API TDynamicMeshOverlay<double, 3>;
template class DYNAMICMESH_API TDynamicMeshOverlay<int, 3>;

template class DYNAMICMESH_API TDynamicMeshVectorOverlay<float, 2, FVector2f>;
template class DYNAMICMESH_API TDynamicMeshVectorOverlay<double, 2, FVector2d>;
template class DYNAMICMESH_API TDynamicMeshVectorOverlay<int, 2, FVector2i>;
template class DYNAMICMESH_API TDynamicMeshVectorOverlay<float, 3, FVector3f>;
template class DYNAMICMESH_API TDynamicMeshVectorOverlay<double, 3, FVector3d>;
template class DYNAMICMESH_API TDynamicMeshVectorOverlay<int, 3, FVector3i>;