// Copyright Epic Games, Inc. All Rights Reserved.

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
int TDynamicMeshOverlay<RealType, ElementSize>::AppendElement(RealType ConstantValue)
{
	int vid = ElementsRefCounts.Allocate();
	int i = ElementSize * vid;
	for (int k = ElementSize - 1; k >= 0; --k)
	{
		Elements.InsertAt(ConstantValue, i + k);
	}
	ParentVertices.InsertAt(FDynamicMesh3::InvalidID, vid);

	//updateTimeStamp(true);
	return vid;
}


template<typename RealType, int ElementSize>
int TDynamicMeshOverlay<RealType, ElementSize>::AppendElement(const RealType* Value)
{
	int vid = ElementsRefCounts.Allocate();
	int i = ElementSize * vid;

	// insert in reverse order so that Resize() is only called once
	for (int k = ElementSize - 1; k >= 0; --k)
	{
		Elements.InsertAt(Value[k], i + k);
	}
	ParentVertices.InsertAt(FDynamicMesh3::InvalidID, vid);

	//updateTimeStamp(true);
	return vid;
}


template<typename RealType, int ElementSize>
EMeshResult TDynamicMeshOverlay<RealType, ElementSize>::InsertElement(int ElementID, const RealType* Value, bool bUnsafe)
{
	if (ElementsRefCounts.IsValid(ElementID))
	{
		return EMeshResult::Failed_VertexAlreadyExists;
	}

	bool bOK = (bUnsafe) ? ElementsRefCounts.AllocateAtUnsafe(ElementID) :
		ElementsRefCounts.AllocateAt(ElementID);
	if (bOK == false)
	{
		return EMeshResult::Failed_CannotAllocateVertex;
	}

	int i = ElementSize * ElementID;
	// insert in reverse order so that Resize() is only called once
	for (int k = ElementSize - 1; k >= 0; --k)
	{
		Elements.InsertAt(Value[k], i + k);
	}

	ParentVertices.InsertAt(FDynamicMesh3::InvalidID, ElementID);

	//UpdateTimeStamp(true, true);
	return EMeshResult::Ok;
}



template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::CreateFromPredicate(TFunctionRef<bool(int ParentVertexIdx, int TriIDA, int TriIDB)> TrisCanShareVertexPredicate, RealType InitElementValue)
{
	ClearElements(); // deletes all elements and initializes triangles to be 1:1 w/ parentmesh IDs
	TArray<int> TrisActiveSubGroup, AppendedElements;
	TArray<int> TriangleIDs, TriangleContigGroupLens;
	TArray<bool> GroupIsLoop;
	for (int VertexID : ParentMesh->VertexIndicesItr())
	{
		
		bool bActiveSubGroupBroken = false;
		ParentMesh->GetVtxContiguousTriangles(VertexID, TriangleIDs, TriangleContigGroupLens, GroupIsLoop);
		int GroupStart = 0;
		for (int GroupIdx = 0; GroupIdx < TriangleContigGroupLens.Num(); GroupIdx++)
		{
			bool bIsLoop = GroupIsLoop[GroupIdx];
			int GroupNum = TriangleContigGroupLens[GroupIdx];
			if (!ensure(GroupNum > 0)) // sanity check; groups should always have at least one element
			{
				continue;
			}

			TrisActiveSubGroup.Reset();
			AppendedElements.Reset();
			TrisActiveSubGroup.SetNumZeroed(GroupNum, false);
			int CurrentGroupID = 0;
			int CurrentGroupRefSubIdx = 0;
			for (int TriSubIdx = 0; TriSubIdx+1 < GroupNum; TriSubIdx++)
			{
				int TriIDA = TriangleIDs[GroupStart + TriSubIdx];
				int TriIDB = TriangleIDs[GroupStart + TriSubIdx + 1];
				bool bCanShare = TrisCanShareVertexPredicate(VertexID, TriIDA, TriIDB);
				if (!bCanShare)
				{
					CurrentGroupID++;
					CurrentGroupRefSubIdx = TriSubIdx + 1;
				}
				
				TrisActiveSubGroup[TriSubIdx + 1] = CurrentGroupID;
			}

			// for loops, merge first and last group if needed
			int NumGroupID = CurrentGroupID + 1;
			if (bIsLoop && TrisActiveSubGroup[0] != TrisActiveSubGroup.Last())
			{
				if (TrisCanShareVertexPredicate(VertexID, TriangleIDs[GroupStart], TriangleIDs[GroupStart + GroupNum - 1]))
				{
					int EndGroupID = TrisActiveSubGroup[GroupNum - 1];
					int StartGroupID = TrisActiveSubGroup[0];
					int TriID0 = TriangleIDs[GroupStart];
					
					for (int Idx = GroupNum - 1; Idx >= 0 && TrisActiveSubGroup[Idx] == EndGroupID; Idx--)
					{
						TrisActiveSubGroup[Idx] = StartGroupID;
					}
					NumGroupID--;
				}
			}

			for (int Idx = 0; Idx < NumGroupID; Idx++)
			{
				AppendedElements.Add(AppendElement(InitElementValue));
			}
			for (int TriSubIdx = 0; TriSubIdx < GroupNum; TriSubIdx++)
			{
				int TriID = TriangleIDs[GroupStart + TriSubIdx];
				FIndex3i TriVertIDs = ParentMesh->GetTriangle(TriID);
				int VertSubIdx = IndexUtil::FindTriIndex(VertexID, TriVertIDs);
				int i = 3 * TriID;
				int ElementIndex = AppendedElements[TrisActiveSubGroup[TriSubIdx]];
				ElementTriangles.InsertAt(ElementIndex, i + VertSubIdx);
				ElementsRefCounts.Increment(ElementIndex);
				ParentVertices.InsertAt(VertexID, ElementIndex);
			}
			GroupStart += GroupNum;
		}
	}
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::SplitVerticesWithPredicate(TFunctionRef<bool(int ElementID, int TriID)> ShouldSplitOutVertex, TFunctionRef<void(int ElementID, int TriID, RealType * FillVect)> GetNewElementValue)
{
	for (int TriID : ParentMesh->TriangleIndicesItr())
	{
		FIndex3i ElTri = GetTriangle(TriID);
		if (ElTri.A < 0)
		{
			// skip un-set triangles
			continue;
		}
		bool TriChanged = false;
		for (int SubIdx = 0; SubIdx < 3; SubIdx++)
		{
			int ElementID = ElTri[SubIdx];
			// by convention for overlays, a ref count of 2 means that only one triangle has the element -- can't split it out further
			if (ElementsRefCounts.GetRefCount(ElementID) <= 2)
			{
				// still set the new value though if the function wants to change it
				if (ShouldSplitOutVertex(ElementID, TriID))
				{
					RealType NewElementData[ElementSize];
					GetNewElementValue(ElementID, TriID, NewElementData);
					SetElement(ElementID, NewElementData);
				}
			}
			if (ShouldSplitOutVertex(ElementID, TriID))
			{
				TriChanged = true;
				RealType NewElementData[ElementSize];
				GetNewElementValue(ElementID, TriID, NewElementData);
				ElTri[SubIdx] = AppendElement(NewElementData);
			}
		}
		if (TriChanged)
		{
			InternalSetTriangle(TriID, ElTri, true);
		}
	}
}


template<typename RealType, int ElementSize>
int TDynamicMeshOverlay<RealType, ElementSize>::SplitElement(int ElementID, const TArrayView<const int>& TrianglesToUpdate)
{
	int ParentID = ParentVertices[ElementID];
	return SplitElementWithNewParent(ElementID, ParentID, TrianglesToUpdate);
}


template<typename RealType, int ElementSize>
int TDynamicMeshOverlay<RealType, ElementSize>::SplitElementWithNewParent(int ElementID, int NewParentID, const TArrayView<const int>& TrianglesToUpdate)
{
	RealType SourceData[ElementSize];
	GetElement(ElementID, SourceData);
	int NewElID = AppendElement(SourceData);
	for (int TriID : TrianglesToUpdate)
	{
		int ElementTriStart = TriID * 3;
		for (int SubIdx = 0; SubIdx < 3; SubIdx++)
		{
			int CurElID = ElementTriangles[ElementTriStart + SubIdx];
			if (CurElID == ElementID)
			{
				ElementsRefCounts.Decrement(ElementID);
				ElementsRefCounts.Increment(NewElID);
				ElementTriangles[ElementTriStart + SubIdx] = NewElID;
			}
		}
	}
	ParentVertices.InsertAt(NewParentID, NewElID);

	checkSlow(ElementsRefCounts.IsValid(ElementID));

	// An element may have become isolated after changing all of its incident triangles. Delete such an element.
	if (ElementsRefCounts.GetRefCount(ElementID) == 1) 
	{
		ElementsRefCounts.Decrement(ElementID);
		ParentVertices[ElementID] = -1;
	}

	return NewElID;
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::SplitBowties()
{
	// arrays for storing contiguous triangle groups from parentmesh
	TArray<int> TrianglesOut, ContiguousGroupLengths;
	TArray<bool> GroupIsLoop;

	// per-vertex element group tracking data, reused in loop below
	TSet<int> ElementIDSeen;
	TArray<int> GroupElementIDs, // stores element IDs 1:1 w/ contiguous triangles in the parent mesh
		SubGroupID, // mapping from GroupElementIDs indices into SubGroupElementIDs indices, giving subgroup membership per triangle
		SubGroupElementIDs; // 1:1 w/ 'subgroups' in the group (e.g. if all triangles in the group all had the same element ID, this would be an array of length 1, just containing that element ID)
	
	for (int VertexID : ParentMesh->VertexIndicesItr())
	{
		ensure(EMeshResult::Ok == ParentMesh->GetVtxContiguousTriangles(VertexID, TrianglesOut, ContiguousGroupLengths, GroupIsLoop));
		int32 NumTris = TrianglesOut.Num();

		ElementIDSeen.Reset();
		// per contiguous group of triangles around vertex in ParentMesh, find contiguous sub-groups in overlay
		for (int32 GroupIdx = 0, NumGroups = ContiguousGroupLengths.Num(), TriSubStart = 0; GroupIdx < NumGroups; GroupIdx++)
		{
			bool bIsLoop = GroupIsLoop[GroupIdx];
			int TriInGroupNum = ContiguousGroupLengths[GroupIdx];
			check(TriInGroupNum > 0);
			int TriSubEnd = TriSubStart + TriInGroupNum;
			
			GroupElementIDs.Reset();
			for (int TriSubIdx = TriSubStart; TriSubIdx < TriSubEnd; TriSubIdx++)
			{
				int TriID = TrianglesOut[TriSubIdx];
				FIndex3i TriVIDs = ParentMesh->GetTriangle(TriID);
				FIndex3i TriEIDs = GetTriangle(TriID);
				int SubIdx = TriVIDs.IndexOf(VertexID);
				GroupElementIDs.Add(TriEIDs[SubIdx]);
			}

			auto IsConnected = [this, &GroupElementIDs, &TrianglesOut, &TriSubStart](int TriOutIdxA, int TriOutIdxB)
			{
				if (GroupElementIDs[TriOutIdxA - TriSubStart] != GroupElementIDs[TriOutIdxB - TriSubStart])
				{
					return false;
				}
				int EdgeID = ParentMesh->FindEdgeFromTriPair(TrianglesOut[TriOutIdxA], TrianglesOut[TriOutIdxB]);
				return EdgeID >= 0 && !IsSeamEdge(EdgeID);
			};

			SubGroupID.Reset(); SubGroupID.SetNum(TriInGroupNum);
			SubGroupElementIDs.Reset();
			int MaxSubID = 0;
			SubGroupID[0] = 0;
			SubGroupElementIDs.Add(GroupElementIDs[0]);
			for (int TriSubIdx = TriSubStart; TriSubIdx+1 < TriSubEnd; TriSubIdx++)
			{
				if (!IsConnected(TriSubIdx, TriSubIdx+1))
				{
					SubGroupElementIDs.Add(GroupElementIDs[TriSubIdx + 1 - TriSubStart]);
					MaxSubID++;
				}
				SubGroupID[TriSubIdx - TriSubStart + 1] = MaxSubID;
			}
			// if group was a loop, need to check if the last sub-group and first sub-group were actually the same group
			if (bIsLoop && MaxSubID > 0 && IsConnected(TriSubStart, TriSubStart + TriInGroupNum - 1))
			{
				int LastGroupID = SubGroupID.Last();
				for (int32 Idx = SubGroupID.Num() - 1; Idx >= 0 && SubGroupID[Idx] == LastGroupID; Idx--)
				{
					SubGroupID[Idx] = 0;
				}
				MaxSubID--;
				SubGroupElementIDs.Pop(false);
			}

			for (int SubID = 0; SubID < SubGroupElementIDs.Num(); SubID++)
			{
				int ElementID = SubGroupElementIDs[SubID];
				if (ElementID < 0)
				{
					continue;		// skip if this is an invalid ElementID (eg from an invalid triangle)
				}
				// split needed the *second* time we see a sub-group using a given ElementID
				if (ElementIDSeen.Contains(ElementID))
				{
					TArray<int> ConnectedTris;
					for (int TriSubIdx = TriSubStart; TriSubIdx < TriSubEnd; TriSubIdx++)
					{
						if (SubID == SubGroupID[TriSubIdx - TriSubStart])
						{
							ConnectedTris.Add(TrianglesOut[TriSubIdx]);
						}
					}
					SplitElement(ElementID, ConnectedTris);
				}
				ElementIDSeen.Add(ElementID);
			}

			TriSubStart = TriSubEnd;
		}
	}
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::InitializeTriangles(int MaxTriangleID)
{
	ElementTriangles.SetNum(MaxTriangleID * 3);
	ElementTriangles.Fill(FDynamicMesh3::InvalidID);
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
void TDynamicMeshOverlay<RealType, ElementSize>::UnsetTriangle(int TriangleID)
{
	int i = 3 * TriangleID;
	if (ElementTriangles[i] == -1)
	{
		return;
	}
	for (int SubIdx = 0; SubIdx < 3; SubIdx++)
	{
		ElementsRefCounts.Decrement(ElementTriangles[i + SubIdx]);

		if (ElementsRefCounts.GetRefCount(ElementTriangles[i + SubIdx]) == 1)
		{
			ElementsRefCounts.Decrement(ElementTriangles[i + SubIdx]);
			ParentVertices[ElementTriangles[i + SubIdx]] = -1;
		}
		ElementTriangles[i + SubIdx] = -1;
	}
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::InternalSetTriangle(int tid, const FIndex3i& tv, bool bIncrementRefCounts)
{
	check(ParentMesh);

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

	if (tv != FDynamicMesh3::InvalidTriangle)
	{
		// Set parent vertex IDs
		const FIndex3i ParentTriangle = ParentMesh->GetTriangle(tid);

		for (int VInd = 0; VInd < 3; ++VInd)
		{
			checkSlow(ParentVertices[tv[VInd]] == ParentTriangle[VInd] || ParentVertices[tv[VInd]] == FDynamicMesh3::InvalidID);
			ParentVertices.InsertAt(ParentTriangle[VInd], tv[VInd]);
		}
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

	bool bASet = IsSetTriangle(et.A), bBSet = IsSetTriangle(et.B);
	if (!bASet || !bBSet) // if either triangle is unset, need different logic for checking if this is a seam
	{
		return (bASet || bBSet); // consider it a seam if only one is unset
	}

	FIndex3i Triangle0 = GetTriangle(et.A);
	FIndex3i BaseTriangle0(ParentVertices[Triangle0.A], ParentVertices[Triangle0.B], ParentVertices[Triangle0.C]);
	int idx_base_a1 = BaseTriangle0.IndexOf(base_a);
	int idx_base_b1 = BaseTriangle0.IndexOf(base_b);

	FIndex3i Triangle1 = GetTriangle(et.B);
	FIndex3i BaseTriangle1(ParentVertices[Triangle1.A], ParentVertices[Triangle1.B], ParentVertices[Triangle1.C]);
	int idx_base_a2 = BaseTriangle1.IndexOf(base_a);
	int idx_base_b2 = BaseTriangle1.IndexOf(base_b);

	return !IndexUtil::SamePairUnordered(Triangle0[idx_base_a1], Triangle0[idx_base_b1], Triangle1[idx_base_a2], Triangle1[idx_base_b2]);


	// TODO: this doesn't seem to work but it should, and would be more efficient:
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
bool TDynamicMeshOverlay<RealType, ElementSize>::IsSeamEndEdge(int eid) const
{
	FIndex2i et = ParentMesh->GetEdgeT(eid);
	if (et.B == FDynamicMesh3::InvalidID)
	{
		return false;
	}

	FIndex2i ev = ParentMesh->GetEdgeV(eid);
	int base_a = ev.A, base_b = ev.B;

	bool bASet = IsSetTriangle(et.A), bBSet = IsSetTriangle(et.B);
	if (!bASet || !bBSet) 
	{
		return false; 
	}

	FIndex3i Triangle0 = GetTriangle(et.A);
	FIndex3i BaseTriangle0(ParentVertices[Triangle0.A], ParentVertices[Triangle0.B], ParentVertices[Triangle0.C]);
	int idx_base_a0 = BaseTriangle0.IndexOf(base_a);
	int idx_base_b0 = BaseTriangle0.IndexOf(base_b);

	FIndex3i Triangle1 = GetTriangle(et.B);
	FIndex3i BaseTriangle1(ParentVertices[Triangle1.A], ParentVertices[Triangle1.B], ParentVertices[Triangle1.C]);
	int idx_base_a1 = BaseTriangle1.IndexOf(base_a);
	int idx_base_b1 = BaseTriangle1.IndexOf(base_b);

	int el_a_tri0 = Triangle0[idx_base_a0];
	int el_b_tri0 = Triangle0[idx_base_b0];
	int el_a_tri1 = Triangle1[idx_base_a1];
	int el_b_tri1 = Triangle1[idx_base_b1];

	bool bIsSeam = !IndexUtil::SamePairUnordered(el_a_tri0, el_b_tri0, el_a_tri1, el_b_tri1);

	bool bIsSeamEnd = false;
	if (bIsSeam)
	{
		// is only one of elements split?
		if ((el_a_tri0 == el_a_tri1 || el_a_tri0 == el_b_tri1) || (el_b_tri0 == el_b_tri1 || el_b_tri0 == el_a_tri1))
		{
			bIsSeamEnd = true;
		}
	}

	return bIsSeamEnd;
}

template<typename RealType, int ElementSize>
bool TDynamicMeshOverlay<RealType, ElementSize>::HasInteriorSeamEdges() const
{
	for (int eid : ParentMesh->EdgeIndicesItr())
	{
		FIndex2i et = ParentMesh->GetEdgeT(eid);
		if (et.B != FDynamicMesh3::InvalidID)
		{
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

			if (!IndexUtil::SamePairUnordered(Triangle0[idx_base_a1], Triangle0[idx_base_b1], Triangle1[idx_base_a2], Triangle1[idx_base_b2]))
			{
				return true;
			}
		}
	}
	return false;
}



template<typename RealType, int ElementSize>
bool TDynamicMeshOverlay<RealType, ElementSize>::IsSeamVertex(int vid, bool bBoundaryIsSeam) const
{
	// @todo can we do this more efficiently? At minimum we are looking up each triangle twice...
	for (int edgeid : ParentMesh->VtxEdgesItr(vid))
	{
		if (!bBoundaryIsSeam && ParentMesh->IsBoundaryEdge(edgeid))
		{
			continue;
		}
		if (IsSeamEdge(edgeid))
		{
			return true;
		}
	}
	return false;
}

template<typename RealType, int ElementSize>
bool TDynamicMeshOverlay<RealType, ElementSize>::IsBowtieInOverlay(int32 VertexID) const
{
	// This is a bit tricky but seems to be correct. If we think of a mesh boundary edge as "one" border edge,
	// and a UV-seam as "two" border edges (one on each side), then we have a non-bowtie configuration if:
	//  1) NumElements == 1 && BorderEdgeCount == 0
	//  2) BorderEdgeCount == 2*NumElements
	// otherwise we have bowties
	// (todo: validate this with brute-force algorithm that finds uv-connected-components of one-ring?)

	int32 NumBoundary = 0;
	int32 NumSeams = 0;
	for (int32 EdgeID : ParentMesh->VtxEdgesItr(VertexID))
	{
		if (ParentMesh->IsBoundaryEdge(EdgeID))
		{
			NumBoundary++;
		} 
		else if (IsSeamEdge(EdgeID))
		{
			NumSeams++;
		}
	}
	int32 NumElements = CountVertexElements(VertexID);
	int32 BorderEdgeCount = NumBoundary + 2 * NumSeams;
	return ! ( (BorderEdgeCount == 0 && NumElements == 1) || (BorderEdgeCount == 2*NumElements) );
}


template<typename RealType, int ElementSize>
bool TDynamicMeshOverlay<RealType, ElementSize>::AreTrianglesConnected(int TriangleID0, int TriangleID1) const
{
	FIndex3i NbrTris = ParentMesh->GetTriNeighbourTris(TriangleID0);
	int NbrIndex = IndexUtil::FindTriIndex(TriangleID1, NbrTris);
	if (NbrIndex != IndexConstants::InvalidID)
	{
		FIndex3i TriEdges = ParentMesh->GetTriEdges(TriangleID0);
		return IsSeamEdge(TriEdges[NbrIndex]) == false;
	}
	return false;
}



template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::GetVertexElements(int vid, TArray<int>& OutElements) const
{
	OutElements.Reset();
	for (int tid : ParentMesh->VtxTrianglesItr(vid))
	{
		if (!IsSetTriangle(tid))
		{
			continue;
		}
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
void TDynamicMeshOverlay<RealType, ElementSize>::GetElementTriangles(int ElementID, TArray<int>& OutTriangles) const
{
	check(ElementsRefCounts.IsValid(ElementID));
	int VertexID = ParentVertices[ElementID];

	for (int TriangleID : ParentMesh->VtxTrianglesItr(VertexID))
	{
		int i = 3 * TriangleID;
		if (ElementTriangles[i] == ElementID || ElementTriangles[i+1] == ElementID || ElementTriangles[i+2] == ElementID)
		{
			OutTriangles.Add(TriangleID);
		}
	}
}


template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnRemoveTriangle(int TriangleID)
{
	FIndex3i Triangle = GetTriangle(TriangleID);
	if (Triangle.A < 0 && Triangle.B < 0 && Triangle.C < 0)
	{
		// if whole triangle has no overlay vertices set, that's OK, just remove nothing
		// (if only *some* of the triangle vertices were < 0, that would be a bug / invalid overlay triangle)
		return;
	}
	InitializeNewTriangle(TriangleID);

	// decrement element refcounts, and free element if it is now unreferenced
	for (int j = 0; j < 3; ++j) 
	{
		int elemid = Triangle[j];
		ElementsRefCounts.Decrement(elemid);
		if (ElementsRefCounts.GetRefCount(elemid) == 1) 
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




template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnSplitEdge(const FDynamicMesh3::FEdgeSplitInfo& splitInfo)
{
	int orig_t0 = splitInfo.OriginalTriangles.A;
	int orig_t1 = splitInfo.OriginalTriangles.B;
	int base_a = splitInfo.OriginalVertices.A;
	int base_b = splitInfo.OriginalVertices.B;

	// special handling if either triangle is unset
	bool bT0Set = IsSetTriangle(orig_t0), bT1Set = orig_t1 >= 0 && IsSetTriangle(orig_t1);
	// insert invalid triangle as needed
	if (!bT0Set)
	{
		InitializeNewTriangle(splitInfo.NewTriangles.A);
	}
	if (!bT1Set && splitInfo.NewTriangles.B >= 0)
	{
		// new triangle is invalid
		InitializeNewTriangle(splitInfo.NewTriangles.B);
	}
	// if neither tri was set, nothing else to do
	if (!bT0Set && !bT1Set)
	{
		return;
	}

	// look up current triangle 0, and infer base triangle 0
	FIndex3i Triangle0(-1, -1, -1);
	int idx_base_a1 = -1, idx_base_b1 = -1;
	int NewElemID = -1;
	if (bT0Set)
	{
		Triangle0 = GetTriangle(orig_t0);
		FIndex3i BaseTriangle0(ParentVertices[Triangle0.A], ParentVertices[Triangle0.B], ParentVertices[Triangle0.C]);
		idx_base_a1 = BaseTriangle0.IndexOf(base_a);
		idx_base_b1 = BaseTriangle0.IndexOf(base_b);
		int idx_base_c = IndexUtil::GetOtherTriIndex(idx_base_a1, idx_base_b1);

		// create new element at lerp position
		NewElemID = AppendElement((RealType)0);
		SetElementFromLerp(NewElemID, Triangle0[idx_base_a1], Triangle0[idx_base_b1], (RealType)splitInfo.SplitT);

		// rewrite triangle 0
		ElementTriangles[3 * orig_t0 + idx_base_b1] = NewElemID;

		// create new triangle 2 w/ correct winding order
		FIndex3i NewTriangle2(NewElemID, Triangle0[idx_base_b1], Triangle0[idx_base_c]);  // mirrors DMesh3::SplitEdge [f,b,c]
		InternalSetTriangle(splitInfo.NewTriangles.A, NewTriangle2, false);

		// update ref counts
		ElementsRefCounts.Increment(NewElemID, 2); // for the two tris on the T0 side
		ElementsRefCounts.Increment(Triangle0[idx_base_c]);
	}

	if (orig_t1 == FDynamicMesh3::InvalidID)
	{
		return;  // we are done if this is a boundary triangle
	}

	// look up current triangle1 and infer base triangle 1
	if (bT1Set)
	{
		FIndex3i Triangle1 = GetTriangle(orig_t1);
		FIndex3i BaseTriangle1(ParentVertices[Triangle1.A], ParentVertices[Triangle1.B], ParentVertices[Triangle1.C]);
		int idx_base_a2 = BaseTriangle1.IndexOf(base_a);
		int idx_base_b2 = BaseTriangle1.IndexOf(base_b);
		int idx_base_d = IndexUtil::GetOtherTriIndex(idx_base_a2, idx_base_b2);

		int OtherNewElemID = NewElemID;

		// if we don't have a shared edge, we need to create another new UV for the other side
		bool bHasSharedUVEdge = bT0Set && IndexUtil::SamePairUnordered(Triangle0[idx_base_a1], Triangle0[idx_base_b1], Triangle1[idx_base_a2], Triangle1[idx_base_b2]);
		if (bHasSharedUVEdge == false)
		{
			// create new element at lerp position
			OtherNewElemID = AppendElement((RealType)0);
			SetElementFromLerp(OtherNewElemID, Triangle1[idx_base_a2], Triangle1[idx_base_b2], (RealType)splitInfo.SplitT);
		}

		// rewrite triangle 1
		ElementTriangles[3 * orig_t1 + idx_base_b2] = OtherNewElemID;

		// create new triangle 3 w/ correct winding order
		FIndex3i NewTriangle3(OtherNewElemID, Triangle1[idx_base_d], Triangle1[idx_base_b2]);  // mirrors DMesh3::SplitEdge [f,d,b]
		InternalSetTriangle(splitInfo.NewTriangles.B, NewTriangle3, false);

		// update ref counts
		ElementsRefCounts.Increment(OtherNewElemID, 2);
		ElementsRefCounts.Increment(Triangle1[idx_base_d]);
	}

}




template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnFlipEdge(const FDynamicMesh3::FEdgeFlipInfo& FlipInfo)
{
	int orig_t0 = FlipInfo.Triangles.A;
	int orig_t1 = FlipInfo.Triangles.B;
	bool bT0Set = IsSetTriangle(orig_t0), bT1Set = IsSetTriangle(orig_t1);
	if (!bT0Set)
	{
		check(!bT1Set); // flipping across a set/unset boundary is not allowed?
		return; // nothing to do on the overlay if both triangles are unset
	}

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






template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnCollapseEdge(const FDynamicMesh3::FEdgeCollapseInfo& collapseInfo)
{

	int tid_removed0 = collapseInfo.RemovedTris.A;
	int tid_removed1 = collapseInfo.RemovedTris.B;
	bool bT0Set = IsSetTriangle(tid_removed0), bT1Set = tid_removed1 >= 0 && IsSetTriangle(tid_removed1);

	int vid_base_kept = collapseInfo.KeptVertex;
	int vid_base_removed = collapseInfo.RemovedVertex;
	

	bool bIsSeam = false;
	bool bIsSeamEnd = false;

	
	// look up triangle 0
	FIndex3i Triangle0(-1,-1,-1), BaseTriangle0(-1,-1,-1);
	int idx_removed_tri0 = -1, idx_kept_tri0 = -1;
	if (bT0Set)
	{
		Triangle0 = GetTriangle(tid_removed0);
		BaseTriangle0 = FIndex3i(ParentVertices[Triangle0.A], ParentVertices[Triangle0.B], ParentVertices[Triangle0.C]);
		idx_kept_tri0 = BaseTriangle0.IndexOf(vid_base_kept);
		idx_removed_tri0    = BaseTriangle0.IndexOf(vid_base_removed);
	}

	// look up triangle 1 if this is not a boundary edge
	FIndex3i Triangle1(-1,-1,-1), BaseTriangle1(-1,-1,-1);
	int idx_removed_tri1 = -1, idx_kept_tri1 = -1;
	if (collapseInfo.bIsBoundary == false && bT1Set)
	{
		Triangle1 = GetTriangle(tid_removed1);
		BaseTriangle1 = FIndex3i(ParentVertices[Triangle1.A], ParentVertices[Triangle1.B], ParentVertices[Triangle1.C]);

		idx_kept_tri1 = BaseTriangle1.IndexOf(vid_base_kept);
		idx_removed_tri1 = BaseTriangle1.IndexOf(vid_base_removed);

		if (bT0Set)
		{
			int el_kept_tri0    = Triangle0[idx_kept_tri0];
			int el_removed_tri0 = Triangle0[idx_removed_tri0];
			int el_kept_tri1    = Triangle1[idx_kept_tri1];
			int el_removed_tri1 = Triangle1[idx_removed_tri1];

			// is this a seam?
			bIsSeam = !IndexUtil::SamePairUnordered(el_kept_tri0, el_removed_tri0, el_kept_tri1, el_removed_tri1);

			if (bIsSeam)
			{
				// is only one of elements split?
				if ((el_kept_tri0 == el_kept_tri1 || el_kept_tri0 == el_removed_tri1) || (el_removed_tri0 == el_kept_tri1 || el_removed_tri0 == el_removed_tri1))
				{
					bIsSeamEnd = true;
				}
			}
		}
	}

	// this should be protected against by calling code.
	//checkSlow(!bIsSeamEnd);

	// need to find the elementid for the "kept" and "removed" vertices that are connected by the edges of T0 and T1.
	// If this edge is :
	//       not a seam - just one kept and one removed element.
	//       a seam end - one (two) kept and two (one) removed elements.
	//       a seam     - two kept and two removed elements.
	// The collapse of a seam end must be protected against by the higher-level code.
	//     There is no sensible way to handle the collapse of a seam end.  Retaining two elements would require some arbitrary split
	//     of the removed element and conversely if one element is retained there is no reason to believe a single element value 
	//     would be a good approximation to collapsing the edges on both sides of the seam end.
	int kept_elemid[2]    = { FDynamicMesh3::InvalidID, FDynamicMesh3::InvalidID };
	int removed_elemid[2] = { FDynamicMesh3::InvalidID, FDynamicMesh3::InvalidID };
	bool bFoundRemovedElement[2] = { false,false };
	bool bFoundKeptElement[2]    = { false, false };
	if (bT0Set)
	{
		kept_elemid[0] = Triangle0[idx_kept_tri0];
		removed_elemid[0] = Triangle0[idx_removed_tri0];
		bFoundKeptElement[0] = bFoundRemovedElement[0] = true;

		checkSlow(kept_elemid[0] != FDynamicMesh3::InvalidID);
		checkSlow(removed_elemid[0] != FDynamicMesh3::InvalidID);
		
	}
	if ((bIsSeam || !bT0Set) && bT1Set)
	{
		kept_elemid[1] = Triangle1[idx_kept_tri1];
		removed_elemid[1] = Triangle1[idx_removed_tri1];
		bFoundKeptElement[1] = bFoundRemovedElement[1] = true;

		checkSlow(kept_elemid[1] != FDynamicMesh3::InvalidID);
		checkSlow(removed_elemid[1] != FDynamicMesh3::InvalidID);
		
	}
	if (!bT0Set && !bT1Set)
	{
		// if neither triangle was set, still need to check surrounding triangles; one or both elements may still be used
		checkSlow(!bFoundRemovedElement[0] && !bFoundRemovedElement[1]);
		
		// note this may do the wrong thing in the case that at least one of the verts has split elements.
		// higher-level code should make sure this doesn't happen.
		
		for (int onering_tid : ParentMesh->VtxTrianglesItr(vid_base_kept))
		{
			if (!IsSetTriangle(onering_tid))
			{
				continue;
			}
			FIndex3i elem_tri = GetTriangle(onering_tid);
			for (int j = 0; j < 3; ++j) 
			{
				int elem_id = elem_tri[j];
				int vid_base = ParentVertices[elem_id];
				if (vid_base == vid_base_removed)
				{
					bFoundRemovedElement[0] = true;
					removed_elemid[0] = elem_id;
				}
				if (vid_base == vid_base_kept)
				{
					bFoundKeptElement[0] = true;
					kept_elemid[0] = elem_id;
				}
			}
		}
	}

	// look for still-existing triangles that have elements linked to the removed vertex.
	// in that case, replace with the element we found

	if (bFoundRemovedElement[0] || bFoundRemovedElement[1])
	{
		for (int onering_tid : ParentMesh->VtxTrianglesItr(vid_base_kept))
		{
			if (!IsSetTriangle(onering_tid))
			{
				continue;
			}
			FIndex3i elem_tri = GetTriangle(onering_tid);
			for (int j = 0; j < 3; ++j)
			{
				int elem_id = elem_tri[j];
				if (ParentVertices[elem_id] == vid_base_removed)
				{
					if (elem_id == removed_elemid[0])
					{
						ElementsRefCounts.Decrement(elem_id);
						ElementTriangles[3 * onering_tid + j] = kept_elemid[0];
						if (bFoundKeptElement[0])
						{
							ElementsRefCounts.Increment(kept_elemid[0]);
						}
					}
					else if (elem_id == removed_elemid[1])
					{
						ElementsRefCounts.Decrement(elem_id);
						ElementTriangles[3 * onering_tid + j] = kept_elemid[1];
						if (bFoundKeptElement[1])
						{
							ElementsRefCounts.Increment(kept_elemid[1]);
						}
					}
					else
					{
						// this could happen if a split edge is adjacent to the edge we collapse
						ParentVertices[elem_id] = vid_base_kept;
					}
				}
			}
		}

		for (int i = 0; i < 2; ++i)
		{
			if (kept_elemid[i] == FDynamicMesh3::InvalidID || removed_elemid[i] == FDynamicMesh3::InvalidID)
			{
				continue;
			}

			// update value of kept element
			SetElementFromLerp(kept_elemid[i], kept_elemid[i], removed_elemid[i], collapseInfo.CollapseT);
		}
	}

	// clear the two triangles we removed
	for (int j = 0; j < 3; ++j) 
	{
		if (bT0Set)
		{

			ElementsRefCounts.Decrement(Triangle0[j]);
			ElementTriangles[3 * tid_removed0 + j] = FDynamicMesh3::InvalidID;
		}
		if (collapseInfo.bIsBoundary == false && bT1Set) 
		{
			ElementsRefCounts.Decrement(Triangle1[j]);
			ElementTriangles[3*tid_removed1 + j] = FDynamicMesh3::InvalidID;
		}
	}

	// if the edge was split, but still shared one element, this should be protected against in the calling code
	if (removed_elemid[1] == removed_elemid[0])
	{
		removed_elemid[1] = FDynamicMesh3::InvalidID;
	}

	// remove the elements associated with the removed vertex
	for (int k = 0; k < 2; ++k)
	{
		if (removed_elemid[k] != FDynamicMesh3::InvalidID)
	{
			int rc = ElementsRefCounts.GetRefCount(removed_elemid[k]);
			check(rc == 1);
			ElementsRefCounts.Decrement(removed_elemid[k]);
			ParentVertices[removed_elemid[k]] = FDynamicMesh3::InvalidID;
		}
	}

}



template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnPokeTriangle(const FDynamicMesh3::FPokeTriangleInfo& PokeInfo)
{
	if (!IsSetTriangle(PokeInfo.OriginalTriangle))
	{
		InitializeNewTriangle(PokeInfo.NewTriangles.A);
		InitializeNewTriangle(PokeInfo.NewTriangles.B);
		return;
	}

	FIndex3i Triangle = GetTriangle(PokeInfo.OriginalTriangle);

	// create new element at barycentric position
	int CenterElemID = AppendElement((RealType)0);
	FVector3<RealType> BaryCoords((RealType)PokeInfo.BaryCoords.X, (RealType)PokeInfo.BaryCoords.Y, (RealType)PokeInfo.BaryCoords.Z);
	SetElementFromBary(CenterElemID, Triangle[0], Triangle[1], Triangle[2], BaryCoords);

	// update orig triangle and two new ones. Winding orders here mirror FDynamicMesh3::PokeTriangle
	InternalSetTriangle(PokeInfo.OriginalTriangle, FIndex3i(Triangle[0], Triangle[1], CenterElemID), false );
	InternalSetTriangle(PokeInfo.NewTriangles.A, FIndex3i(Triangle[1], Triangle[2], CenterElemID), false);
	InternalSetTriangle(PokeInfo.NewTriangles.B, FIndex3i(Triangle[2], Triangle[0], CenterElemID), false);

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

	for (int i = 0; i < 2; i++)
	{
		int KeptVID = MergeInfo.KeptVerts[i];
		int RemovedVID = MergeInfo.RemovedVerts[i];
		if (RemovedVID == FDynamicMesh3::InvalidID)
		{
			continue;
		}
		// this for loop is very similar to GetVertexElements() but accounts for the base mesh already being updated
		for (int TID : ParentMesh->VtxTrianglesItr(KeptVID)) // only care about triangles connected to the *new* vertex; these are updated
		{
			if (!IsSetTriangle(TID))
			{
				continue;
			}
			FIndex3i Triangle = GetTriangle(TID);
			for (int j = 0; j < 3; ++j)
			{
				// though the ParentMesh vertex is NewVertex in the source mesh, it is still OriginalVertex in the ParentVertices array (since that hasn't been updated yet)
				if (Triangle[j] != FDynamicMesh3::InvalidID && ParentVertices[Triangle[j]] == RemovedVID)
				{
					ParentVertices[Triangle[j]] = KeptVID;
				}
			}
		}
	}
}



template<typename RealType, int ElementSize>
void TDynamicMeshOverlay<RealType, ElementSize>::OnSplitVertex(const DynamicMeshInfo::FVertexSplitInfo& SplitInfo, const TArrayView<const int>& TrianglesToUpdate)
{
	TArray<int> OutElements;

	// this for loop is very similar to GetVertexElements() but accounts for the base mesh already being updated
	for (int tid : ParentMesh->VtxTrianglesItr(SplitInfo.NewVertex)) // only care about triangles connected to the *new* vertex; these are updated
	{
		if (!IsSetTriangle(tid))
		{
			continue;
		}
		FIndex3i Triangle = GetTriangle(tid);
		for (int j = 0; j < 3; ++j)
		{
			// though the ParentMesh vertex is NewVertex in the source mesh, it is still OriginalVertex in the ParentVertices array (since that hasn't been updated yet)
			if (Triangle[j] != FDynamicMesh3::InvalidID && ParentVertices[Triangle[j]] == SplitInfo.OriginalVertex)
			{
				OutElements.AddUnique(Triangle[j]);
			}
		}
	}

	for (int ElementID : OutElements)
	{
		// Note: TrianglesToUpdate will include triangles that don't include the element, but that's ok; it just won't find any elements to update for those
		//			(and this should be cheaper than constructing a new array for every element)
		SplitElementWithNewParent(ElementID, SplitInfo.NewVertex, TrianglesToUpdate);
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

	// check that parent vtx of a non-isolated element is actually a vertex
	for (int elemid : ElementIndicesItr())
	{
		int ParentVID = GetParentVertex(elemid);
		bool bParentIsVertex = ParentMesh->IsVertex(ParentVID);
		bool bElementIsIsolated = (ElementsRefCounts.GetRefCount(elemid) == 1);

		// bParentIsVertex XOR bElementIsIsolated
		if ( bParentIsVertex )
		{
			CheckOrFailF(!bElementIsIsolated);
		}
		else
		{
			CheckOrFailF(bElementIsIsolated);
		}
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