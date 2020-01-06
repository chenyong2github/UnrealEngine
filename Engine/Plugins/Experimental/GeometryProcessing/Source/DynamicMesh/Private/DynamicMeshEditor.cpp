// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "DynamicMeshEditor.h"
#include "DynamicMeshAttributeSet.h"
#include "Util/BufferUtil.h"
#include "MeshRegionBoundaryLoops.h"
#include "DynamicSubmesh3.h"


void FMeshIndexMappings::Initialize(FDynamicMesh3* Mesh)
{
	if (Mesh->HasAttributes())
	{
		FDynamicMeshAttributeSet* Attribs = Mesh->Attributes();
		UVMaps.SetNum(Attribs->NumUVLayers());
		NormalMaps.SetNum(Attribs->NumNormalLayers());
	}
}





void FDynamicMeshEditResult::GetAllTriangles(TArray<int>& TrianglesOut) const
{
	BufferUtil::AppendElements(TrianglesOut, NewTriangles);

	int NumQuads = NewQuads.Num();
	for (int k = 0; k < NumQuads; ++k)
	{
		TrianglesOut.Add(NewQuads[k].A);
		TrianglesOut.Add(NewQuads[k].B);
	}
	int NumPolys = NewPolygons.Num();
	for (int k = 0; k < NumPolys; ++k)
	{
		BufferUtil::AppendElements(TrianglesOut, NewPolygons[k]);
	}
}






bool FDynamicMeshEditor::StitchVertexLoopsMinimal(const TArray<int>& Loop1, const TArray<int>& Loop2, FDynamicMeshEditResult& ResultOut)
{
	int N = Loop1.Num();
	checkf(N == Loop2.Num(), TEXT("FDynamicMeshEditor::StitchLoop: loops are not the same length!"));
	if (N != Loop2.Num())
	{
		return false;
	}

	ResultOut.NewQuads.Reserve(N);
	ResultOut.NewGroups.Reserve(N);

	int i = 0;
	for (; i < N; ++i) 
	{
		int a = Loop1[i];
		int b = Loop1[(i + 1) % N];
		int c = Loop2[i];
		int d = Loop2[(i + 1) % N];

		int NewGroupID = Mesh->AllocateTriangleGroup();
		ResultOut.NewGroups.Add(NewGroupID);

		FIndex3i t1(b, a, d);
		int tid1 = Mesh->AppendTriangle(t1, NewGroupID);

		FIndex3i t2(a, c, d);
		int tid2 = Mesh->AppendTriangle(t2, NewGroupID);

		ResultOut.NewQuads.Add(FIndex2i(tid1, tid2));

		if (tid1 < 0 || tid2 < 0)
		{
			goto operation_failed;
		}
	}

	return true;

operation_failed:
	// remove what we added so far
	if (ResultOut.NewQuads.Num()) 
	{
		TArray<int> Triangles; Triangles.Reserve(2*ResultOut.NewQuads.Num());
		for (const FIndex2i& QuadTriIndices : ResultOut.NewQuads)
		{
			Triangles.Add(QuadTriIndices.A);
			Triangles.Add(QuadTriIndices.B);
		}
		if (!RemoveTriangles(Triangles, false))
		{
			checkf(false, TEXT("FDynamicMeshEditor::StitchLoop: failed to add all triangles, and also failed to back out changes."));
		}
	}
	return false;
}



bool FDynamicMeshEditor::StitchSparselyCorrespondedVertexLoops(const TArray<int>& VertexIDs1, const TArray<int>& MatchedIndices1, const TArray<int>& VertexIDs2, const TArray<int>& MatchedIndices2, FDynamicMeshEditResult& ResultOut)
{
	int CorrespondN = MatchedIndices1.Num();
	if (!ensureMsgf(CorrespondN == MatchedIndices2.Num(), TEXT("FDynamicMeshEditor::StitchSparselyCorrespondedVertices: correspondence arrays are not the same length!")))
	{
		return false;
	}
	// TODO: support case of only one corresponded vertex & a connecting a full loop around?
	// this requires allowing start==end to not immediately stop the walk ...
	if (!ensureMsgf(CorrespondN >= 2, TEXT("Must have at least two corresponded vertices")))
	{
		return false;
	}
	ResultOut.NewGroups.Reserve(CorrespondN);

	int i = 0;
	for (; i < CorrespondN; ++i) 
	{
		int Starts[2] { MatchedIndices1[i], MatchedIndices2[i] };
		int Ends[2] { MatchedIndices1[(i + 1) % CorrespondN], MatchedIndices2[(i + 1) % CorrespondN] };

		auto GetWrappedSpanLen = [](const FDynamicMesh3* M, const TArray<int>& VertexIDs, int StartInd, int EndInd)
		{
			float LenTotal = 0;
			FVector3d V = M->GetVertex(VertexIDs[StartInd]);
			for (int Ind = StartInd, IndNext; Ind != EndInd;)
			{
				IndNext = (Ind + 1) % VertexIDs.Num();
				FVector3d VNext = M->GetVertex(VertexIDs[IndNext]);
				LenTotal += V.Distance(VNext);
				Ind = IndNext;
				V = VNext;
			}
			return LenTotal;
		};
		float LenTotal[2] { GetWrappedSpanLen(Mesh, VertexIDs1, Starts[0], Ends[0]), GetWrappedSpanLen(Mesh, VertexIDs2, Starts[1], Ends[1]) };
		float LenAlong[2] { FMathf::Epsilon, FMathf::Epsilon };
		LenTotal[0] += FMathf::Epsilon;
		LenTotal[1] += FMathf::Epsilon;


		int NewGroupID = Mesh->AllocateTriangleGroup();
		ResultOut.NewGroups.Add(NewGroupID);
		
		int Walks[2]{ Starts[0], Starts[1] };
		FVector3d Vertex[2]{ Mesh->GetVertex(VertexIDs1[Starts[0]]), Mesh->GetVertex(VertexIDs2[Starts[1]]) };
		while (Walks[0] != Ends[0] || Walks[1] != Ends[1])
		{
			float PctAlong[2]{ LenAlong[0] / LenTotal[0], LenAlong[1] / LenTotal[1] };
			bool bAdvanceSecond = (Walks[0] == Ends[0] || (Walks[1] != Ends[1] && PctAlong[0] > PctAlong[1]));
			FIndex3i Tri(VertexIDs1[Walks[0]], VertexIDs2[Walks[1]], -1);
			if (!bAdvanceSecond)
			{
				Walks[0] = (Walks[0] + 1) % VertexIDs1.Num();
				
				Tri.C = VertexIDs1[Walks[0]];
				FVector3d NextV = Mesh->GetVertex(Tri.C);
				LenAlong[0] += NextV.Distance(Vertex[0]);
				Vertex[0] = NextV;
			}
			else
			{
				Walks[1] = (Walks[1] + 1) % VertexIDs2.Num();
				Tri.C = VertexIDs2[Walks[1]];
				FVector3d NextV = Mesh->GetVertex(Tri.C);
				LenAlong[1] += NextV.Distance(Vertex[1]);
				Vertex[1] = NextV;
			}
			int Tid = Mesh->AppendTriangle(Tri, NewGroupID);
			ResultOut.NewTriangles.Add(Tid);

			if (Tid < 0)
			{
				goto operation_failed;
			}
		}
	}

	return true;

operation_failed:
	// remove what we added so far
	if (ResultOut.NewTriangles.Num()) 
	{
		ensureMsgf(RemoveTriangles(ResultOut.NewTriangles, false), TEXT("FDynamicMeshEditor::StitchSparselyCorrespondedVertexLoops: failed to add all triangles, and also failed to back out changes."));
	}
	return false;
}

bool FDynamicMeshEditor::AddTriangleFan_OrderedVertexLoop(int CenterVertex, const TArray<int>& VertexLoop, int GroupID, FDynamicMeshEditResult& ResultOut)
{
	if (GroupID == -1)
	{
		GroupID = Mesh->AllocateTriangleGroup();
		ResultOut.NewGroups.Add(GroupID);
	}

	int N = VertexLoop.Num();
	ResultOut.NewTriangles.Reserve(N);

	int i = 0;
	for (i = 0; i < N; ++i)
	{
		int A = VertexLoop[i];
		int B = VertexLoop[(i + 1) % N];

		FIndex3i NewT(CenterVertex, B, A);
		int NewTID = Mesh->AppendTriangle(NewT, GroupID);
		if (NewTID < 0)
		{
			goto operation_failed;
		}

		ResultOut.NewTriangles.Add(NewTID);
	}

	return true;

operation_failed:
	// remove what we added so far
	if (!RemoveTriangles(ResultOut.NewTriangles, false))
	{
		checkf(false, TEXT("FDynamicMeshEditor::AddTriangleFan: failed to add all triangles, and also failed to back out changes."));
	}
	return false;
}



bool FDynamicMeshEditor::RemoveTriangles(const TArray<int>& Triangles, bool bRemoveIsolatedVerts)
{
	return RemoveTriangles(Triangles, bRemoveIsolatedVerts, [](int) {});
}


bool FDynamicMeshEditor::RemoveTriangles(const TArray<int>& Triangles, bool bRemoveIsolatedVerts, TFunctionRef<void(int)> OnRemoveTriFunc)
{
	bool bAllOK = true;
	int NumTriangles = Triangles.Num();
	for (int i = 0; i < NumTriangles; ++i)
	{
		if (Mesh->IsTriangle(Triangles[i]) == false)
		{
			continue;
		}

		OnRemoveTriFunc(Triangles[i]);

		EMeshResult result = Mesh->RemoveTriangle(Triangles[i], bRemoveIsolatedVerts, false);
		if (result != EMeshResult::Ok)
		{
			bAllOK = false;
		}
	}
	return bAllOK;
}





/**
 * Make a copy of provided triangles, with new vertices. You provide IndexMaps because
 * you know if you are doing a small subset or a full-mesh-copy.
 */
void FDynamicMeshEditor::DuplicateTriangles(const TArray<int>& Triangles, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	ResultOut.Reset();
	IndexMaps.Initialize(Mesh);

	for (int TriangleID : Triangles) 
	{
		FIndex3i Tri = Mesh->GetTriangle(TriangleID);

		int NewGroupID = FindOrCreateDuplicateGroup(TriangleID, IndexMaps, ResultOut);

		FIndex3i NewTri;
		NewTri[0] = FindOrCreateDuplicateVertex(Tri[0], IndexMaps, ResultOut);
		NewTri[1] = FindOrCreateDuplicateVertex(Tri[1], IndexMaps, ResultOut);
		NewTri[2] = FindOrCreateDuplicateVertex(Tri[2], IndexMaps, ResultOut);

		int NewTriangleID = Mesh->AppendTriangle(NewTri, NewGroupID);
		IndexMaps.SetTriangle(TriangleID, NewTriangleID);
		ResultOut.NewTriangles.Add(NewTriangleID);

		CopyAttributes(TriangleID, NewTriangleID, IndexMaps, ResultOut);

		//Mesh->CheckValidity(true);
	}

}





bool FDynamicMeshEditor::DisconnectTriangles(const TArray<int>& Triangles, TArray<FLoopPairSet>& LoopSetOut)
{
	check(Mesh->HasAttributes() == false);  // not supported yet

	// find the region boundary loops
	FMeshRegionBoundaryLoops RegionLoops(Mesh, Triangles, false);
	bool bOK = RegionLoops.Compute();
	if (!ensure(bOK))
	{
		return false;
	}
	TArray<FEdgeLoop>& Loops = RegionLoops.Loops;

	// need to test Contains() many times
	TSet<int> TriangleSet;
	TriangleSet.Reserve(Triangles.Num() * 3);
	for (int TriID : Triangles)
	{
		TriangleSet.Add(TriID);
	}

	// process each loop island
	int NumLoops = Loops.Num();
	LoopSetOut.SetNum(NumLoops);
	for ( int li = 0; li < NumLoops; ++li)
	{
		FEdgeLoop& Loop = Loops[li];
		FLoopPairSet& LoopPair = LoopSetOut[li];
		LoopPair.LoopA = Loop;

		// duplicate the vertices
		int NumVertices = Loop.Vertices.Num();
		TMap<int, int> LoopVertexMap; LoopVertexMap.Reserve(NumVertices);
		TArray<int> NewVertexLoop; NewVertexLoop.SetNum(NumVertices);
		for (int vi = 0; vi < NumVertices; ++vi)
		{
			int VertID = Loop.Vertices[vi];
			int NewVertID = Mesh->AppendVertex(*Mesh, VertID);
			LoopVertexMap.Add(Loop.Vertices[vi], NewVertID);
			NewVertexLoop[vi] = NewVertID;
		}

		// for each border triangle, rewrite vertices
		int NumEdges = Loop.Edges.Num();
		for (int ei = 0; ei < NumEdges; ++ei)
		{
			int EdgeID = Loop.Edges[ei];
			FIndex2i EdgeTris = Mesh->GetEdgeT(EdgeID);
			int EditTID = TriangleSet.Contains(EdgeTris.A) ? EdgeTris.A : EdgeTris.B;
			if (EditTID == FDynamicMesh3::InvalidID)
			{
				continue;		// happens on final edge, and on input boundary edges
			}

			FIndex3i OldTri = Mesh->GetTriangle(EditTID);
			FIndex3i NewTri = OldTri;
			int Modified = 0;
			for (int j = 0; j < 3; ++j)
			{
				const int* NewVertID = LoopVertexMap.Find(OldTri[j]);
				if (NewVertID != nullptr)
				{
					NewTri[j] = *NewVertID;
					++Modified;
				}
			}
			if (Modified > 0)
			{
				Mesh->SetTriangle(EditTID, NewTri, false);
			}
		}

		LoopPair.LoopB.InitializeFromVertices(Mesh, NewVertexLoop, false);
	}

	return true;
}


void FDynamicMeshEditor::DisconnectTriangles(const TArray<int>& Triangles, bool bPreventBowties)
{
	TSet<int> TriSet, BoundaryVerts;
	TArray<int> NewVerts, OldVertsThatSplit;
	TArray<int> FilteredTriangles;
	DynamicMeshInfo::FVertexSplitInfo SplitInfo;

	TriSet.Append(Triangles);
	for (int TID : Triangles)
	{
		FIndex3i Nbrs = Mesh->GetTriNeighbourTris(TID);
		FIndex3i Tri = Mesh->GetTriangle(TID);
		for (int SubIdx = 0; SubIdx < 3; SubIdx++)
		{
			int NeighborTID = Nbrs[SubIdx];
			if (!TriSet.Contains(NeighborTID))
			{
				BoundaryVerts.Add(Tri[SubIdx]);
				BoundaryVerts.Add(Tri[(SubIdx + 1) % 3]);
			}
		}
	}
	for (int VID : BoundaryVerts)
	{
		FilteredTriangles.Reset();
		int TriRingCount = 0;
		for (int RingTID : Mesh->VtxTrianglesItr(VID))
		{
			if (TriSet.Contains(RingTID))
			{
				FilteredTriangles.Add(RingTID);
			}
			TriRingCount++;
		}

		if (FilteredTriangles.Num() < TriRingCount)
		{
			checkSlow(!Mesh->SplitVertexWouldLeaveIsolated(VID, FilteredTriangles));
			ensure(EMeshResult::Ok == Mesh->SplitVertex(VID, FilteredTriangles, SplitInfo));
			NewVerts.Add(SplitInfo.NewVertex);
			OldVertsThatSplit.Add(SplitInfo.OriginalVertex);
		}
	}
	if (bPreventBowties)
	{
		FDynamicMeshEditResult Result;
		for (int VID : OldVertsThatSplit)
		{
			SplitBowties(VID, Result);
			Result.Reset(); // don't actually keep results; they are not used in this fn
		}
		for (int VID : NewVerts)
		{
			SplitBowties(VID, Result);
			Result.Reset(); // don't actually keep results; they are not used in this fn
		}
	}
}




void FDynamicMeshEditor::SplitBowties(FDynamicMeshEditResult& ResultOut)
{
	ResultOut.Reset();
	TSet<int> AddedVerticesWithIDLessThanMax; // added vertices that we can't filter just by checking against original max id; this will be empty for compact meshes
	for (int VertexID = 0, OriginalMaxID = Mesh->MaxVertexID(); VertexID < OriginalMaxID; VertexID++)
	{
		if (!Mesh->IsVertex(VertexID) || AddedVerticesWithIDLessThanMax.Contains(VertexID))
		{
			continue;
		}
		int32 NumVertsBefore = ResultOut.NewVertices.Num();
		// TODO: may be faster to inline this call to reuse the contiguous triangle arrays?
		SplitBowties(VertexID, ResultOut);
		for (int Idx = NumVertsBefore; Idx < ResultOut.NewVertices.Num(); Idx++)
		{
			if (ResultOut.NewVertices[Idx] < OriginalMaxID)
			{
				AddedVerticesWithIDLessThanMax.Add(ResultOut.NewVertices[Idx]);
			}
		}
	}
}



void FDynamicMeshEditor::SplitBowties(int VertexID, FDynamicMeshEditResult& ResultOut)
{
	TArray<int> TrianglesOut, ContiguousGroupLengths;
	TArray<bool> GroupIsLoop;
	DynamicMeshInfo::FVertexSplitInfo SplitInfo;
	check(Mesh->IsVertex(VertexID));
	if (ensure(EMeshResult::Ok == Mesh->GetVtxContiguousTriangles(VertexID, TrianglesOut, ContiguousGroupLengths, GroupIsLoop)))
	{
		if (ContiguousGroupLengths.Num() > 1)
		{
			// is bowtie
			for (int GroupIdx = 1, GroupStartIdx = ContiguousGroupLengths[0]; GroupIdx < ContiguousGroupLengths.Num(); GroupStartIdx += ContiguousGroupLengths[GroupIdx++])
			{
				ensure(EMeshResult::Ok == Mesh->SplitVertex(VertexID, TArrayView<const int>(TrianglesOut.GetData() + GroupStartIdx, ContiguousGroupLengths[GroupIdx]), SplitInfo));
				ResultOut.NewVertices.Add(SplitInfo.NewVertex);
			}
		}
	}
}


bool FDynamicMeshEditor::ReinsertSubmesh(const FDynamicSubmesh3& Region, FOptionallySparseIndexMap& SubToNewV, TArray<int>* new_tris, EDuplicateTriBehavior DuplicateBehavior)
{
	check(Region.GetBaseMesh() == Mesh);
	const FDynamicMesh3& Sub = Region.GetSubmesh();
	bool bAllOK = true;

	FIndexFlagSet done_v(Sub.MaxVertexID(), Sub.TriangleCount()/2);
	SubToNewV.Initialize(Sub.MaxVertexID(), Sub.VertexCount());

	int NT = Sub.MaxTriangleID();
	for (int ti = 0; ti < NT; ++ti )
	{
		if (Sub.IsTriangle(ti) == false)
		{
			continue;
		}

		FIndex3i sub_t = Sub.GetTriangle(ti);
		int gid = Sub.GetTriangleGroup(ti);

		FIndex3i new_t = FIndex3i::Zero();
		for ( int j = 0; j < 3; ++j )
		{
			int sub_v = sub_t[j];
			int new_v = -1;
			if (done_v[sub_v] == false)
			{
				// first check if this is a boundary vtx on submesh and maps to a bdry vtx on base mesh
				if (Sub.IsBoundaryVertex(sub_v))
				{
					int base_v = Region.MapVertexToBaseMesh(sub_v);
					if (base_v >= 0 && Mesh->IsVertex(base_v) && Region.InBaseBorderVertices(base_v) == true)
					{
						// [RMS] this should always be true, but assert in tests to find out
						if (ensure(Mesh->IsBoundaryVertex(base_v)))
						{
							new_v = base_v;
						}
					}
				}

				// if that didn't happen, append new vtx
				if (new_v == -1)
				{
					new_v = Mesh->AppendVertex(Sub, sub_v);
				}

				SubToNewV.Set(sub_v, new_v);
				done_v.Add(sub_v);

			}
			else
			{
				new_v = SubToNewV[sub_v];
			}

			new_t[j] = new_v;
		}

		// try to handle duplicate-tri case
		if (DuplicateBehavior == EDuplicateTriBehavior::EnsureContinue)
		{
			ensure(Mesh->FindTriangle(new_t.A, new_t.B, new_t.C) == FDynamicMesh3::InvalidID);
		}
		else
		{
			int existing_tid = Mesh->FindTriangle(new_t.A, new_t.B, new_t.C);
			if (existing_tid != FDynamicMesh3::InvalidID)
			{
				if (DuplicateBehavior == EDuplicateTriBehavior::EnsureAbort)
				{
					ensure(false);
					return false;
				}
				else if (DuplicateBehavior == EDuplicateTriBehavior::UseExisting)
				{
					if (new_tris)
					{
						new_tris->Add(existing_tid);
					}
					continue;
				}
				else if (DuplicateBehavior == EDuplicateTriBehavior::Replace)
				{
					Mesh->RemoveTriangle(existing_tid, false);
				}
			}
		}


		int new_tid = Mesh->AppendTriangle(new_t, gid);
		ensure(new_tid != FDynamicMesh3::InvalidID && new_tid != FDynamicMesh3::NonManifoldID);
		if (!Mesh->IsTriangle(new_tid))
		{
			bAllOK = false;
		}

		if (new_tris)
		{
			new_tris->Add(new_tid);
		}
	}

	return bAllOK;
}



FVector3f FDynamicMeshEditor::ComputeAndSetQuadNormal(const FIndex2i& QuadTris, bool bIsPlanar)
{
	FVector3f Normal(0, 0, 1);
	if (bIsPlanar)
	{
		Normal = (FVector3f)Mesh->GetTriNormal(QuadTris.A);
	}
	else
	{
		Normal = (FVector3f)Mesh->GetTriNormal(QuadTris.A);
		Normal += (FVector3f)Mesh->GetTriNormal(QuadTris.B);
		Normal.Normalize();
	}
	SetQuadNormals(QuadTris, Normal);
	return Normal;
}




void FDynamicMeshEditor::SetQuadNormals(const FIndex2i& QuadTris, const FVector3f& Normal)
{
	check(Mesh->HasAttributes());
	FDynamicMeshNormalOverlay* Normals = Mesh->Attributes()->PrimaryNormals();

	FIndex3i Triangle1 = Mesh->GetTriangle(QuadTris.A);

	FIndex3i NormalTriangle1;
	NormalTriangle1[0] = Normals->AppendElement(Normal, Triangle1[0]);
	NormalTriangle1[1] = Normals->AppendElement(Normal, Triangle1[1]);
	NormalTriangle1[2] = Normals->AppendElement(Normal, Triangle1[2]);
	Normals->SetTriangle(QuadTris.A, NormalTriangle1);

	if (Mesh->IsTriangle(QuadTris.B))
	{
		FIndex3i Triangle2 = Mesh->GetTriangle(QuadTris.B);
		FIndex3i NormalTriangle2;
		for (int j = 0; j < 3; ++j)
		{
			int i = Triangle1.IndexOf(Triangle2[j]);
			if (i == -1)
			{
				NormalTriangle2[j] = Normals->AppendElement(Normal, Triangle2[j]);
			}
			else
			{
				NormalTriangle2[j] = NormalTriangle1[i];
			}
		}
		Normals->SetTriangle(QuadTris.B, NormalTriangle2);
	}

}


void FDynamicMeshEditor::SetTriangleNormals(const TArray<int>& Triangles, const FVector3f& Normal)
{
	check(Mesh->HasAttributes());
	FDynamicMeshNormalOverlay* Normals = Mesh->Attributes()->PrimaryNormals();

	TMap<int, int> Vertices;

	for (int tid : Triangles)
	{
		FIndex3i BaseTri = Mesh->GetTriangle(tid);
		FIndex3i ElemTri;
		for (int j = 0; j < 3; ++j)
		{
			const int* FoundElementID = Vertices.Find(BaseTri[j]);
			if (FoundElementID == nullptr)
			{
				ElemTri[j] = Normals->AppendElement(Normal, BaseTri[j]);
				Vertices.Add(BaseTri[j], ElemTri[j]);
			}
			else
			{
				ElemTri[j] = *FoundElementID;
			}
		}
		Normals->SetTriangle(tid, ElemTri);
	}
}


void FDynamicMeshEditor::SetTriangleUVsFromProjection(const TArray<int>& Triangles, const FFrame3d& ProjectionFrame, float UVScaleFactor, const FVector2f& UVTranslation, int UVLayerIndex)
{
	if (!Triangles.Num())
	{
		return;
	}

	check(Mesh->HasAttributes() && Mesh->Attributes()->NumUVLayers() > UVLayerIndex);
	FDynamicMeshUVOverlay* UVs = Mesh->Attributes()->GetUVLayer(UVLayerIndex);

	TMap<int, int> BaseToOverlayVIDMap;
	TArray<int> AllUVIndices;

	FAxisAlignedBox2f UVBounds(FAxisAlignedBox2f::Empty());

	for (int TID : Triangles)
	{
		FIndex3i BaseTri = Mesh->GetTriangle(TID);
		FIndex3i ElemTri;
		for (int j = 0; j < 3; ++j)
		{
			const int* FoundElementID = BaseToOverlayVIDMap.Find(BaseTri[j]);
			if (FoundElementID == nullptr)
			{
				FVector2f UV = (FVector2f)ProjectionFrame.ToPlaneUV(Mesh->GetVertex(BaseTri[j]), 2);
				UVBounds.Contain(UV);
				ElemTri[j] = UVs->AppendElement(UV, BaseTri[j]);
				AllUVIndices.Add(ElemTri[j]);
				BaseToOverlayVIDMap.Add(BaseTri[j], ElemTri[j]);
			}
			else
			{
				ElemTri[j] = *FoundElementID;
			}
		}
		UVs->SetTriangle(TID, ElemTri);
	}

	// shift UVs so that their bbox min-corner is at origin and scaled by external scale factor
	for (int UVID : AllUVIndices)
	{
		FVector2f UV = UVs->GetElement(UVID);
		FVector2f TransformedUV = (UV - UVBounds.Min) * UVScaleFactor;
		TransformedUV += UVTranslation;
		UVs->SetElement(UVID, TransformedUV);
	}
}


void FDynamicMeshEditor::SetQuadUVsFromProjection(const FIndex2i& QuadTris, const FFrame3d& ProjectionFrame, float UVScaleFactor, const FVector2f& UVTranslation, int UVLayerIndex)
{
	check(Mesh->HasAttributes() && Mesh->Attributes()->NumUVLayers() > UVLayerIndex );
	FDynamicMeshUVOverlay* UVs = Mesh->Attributes()->GetUVLayer(UVLayerIndex);

	FIndex4i AllUVIndices(-1, -1, -1, -1);
	FVector2f AllUVs[4];

	// project first triangle
	FIndex3i Triangle1 = Mesh->GetTriangle(QuadTris.A);
	FIndex3i UVTriangle1;
	for (int j = 0; j < 3; ++j)
	{
		FVector2f UV = (FVector2f)ProjectionFrame.ToPlaneUV(Mesh->GetVertex(Triangle1[j]), 2);
		UVTriangle1[j] = UVs->AppendElement(UV, Triangle1[j]);
		AllUVs[j] = UV;
		AllUVIndices[j] = UVTriangle1[j];
	}
	UVs->SetTriangle(QuadTris.A, UVTriangle1);

	// project second triangle
	if (Mesh->IsTriangle(QuadTris.B))
	{
		FIndex3i Triangle2 = Mesh->GetTriangle(QuadTris.B);
		FIndex3i UVTriangle2;
		for (int j = 0; j < 3; ++j)
		{
			int i = Triangle1.IndexOf(Triangle2[j]);
			if (i == -1)
			{
				FVector2f UV = (FVector2f)ProjectionFrame.ToPlaneUV(Mesh->GetVertex(Triangle2[j]), 2);
				UVTriangle2[j] = UVs->AppendElement(UV, Triangle2[j]);
				AllUVs[3] = UV;
				AllUVIndices[3] = UVTriangle2[j];
			}
			else
			{
				UVTriangle2[j] = UVTriangle1[i];
			}
		}
		UVs->SetTriangle(QuadTris.B, UVTriangle2);
	}

	// shift UVs so that their bbox min-corner is at origin and scaled by external scale factor
	FAxisAlignedBox2f UVBounds(FAxisAlignedBox2f::Empty());
	UVBounds.Contain(AllUVs[0]);  UVBounds.Contain(AllUVs[1]);  UVBounds.Contain(AllUVs[2]);
	if (AllUVIndices[3] != -1)
	{
		UVBounds.Contain(AllUVs[3]);
	}
	for (int j = 0; j < 4; ++j)
	{
		if (AllUVIndices[j] != -1)
		{
			FVector2f TransformedUV = (AllUVs[j] - UVBounds.Min) * UVScaleFactor;
			TransformedUV += UVTranslation;
			UVs->SetElement(AllUVIndices[j], TransformedUV);
		}
	}
}


void FDynamicMeshEditor::RescaleAttributeUVs(float UVScale, bool bWorldSpace, int UVLayerIndex, TOptional<FTransform3d> ToWorld)
{
	check(Mesh->HasAttributes() && Mesh->Attributes()->NumUVLayers() > UVLayerIndex );
	FDynamicMeshUVOverlay* UVs = Mesh->Attributes()->GetUVLayer(UVLayerIndex);

	if (bWorldSpace)
	{
		FVector2f TriUVs[3];
		FVector3d TriVs[3];
		float TotalEdgeUVLen = 0;
		double TotalEdgeLen = 0;
		for (int TID : Mesh->TriangleIndicesItr())
		{
			UVs->GetTriElements(TID, TriUVs[0], TriUVs[1], TriUVs[2]);
			Mesh->GetTriVertices(TID, TriVs[0], TriVs[1], TriVs[2]);
			if (ToWorld.IsSet())
			{
				for (int i = 0; i < 3; i++)
				{
					TriVs[i] = ToWorld->TransformPosition(TriVs[i]);
				}
			}
			for (int j = 2, i = 0; i < 3; j = i++)
			{
				TotalEdgeUVLen += TriUVs[j].Distance(TriUVs[i]);
				TotalEdgeLen += TriVs[j].Distance(TriVs[i]);
			}
		}
		if (TotalEdgeUVLen > KINDA_SMALL_NUMBER)
		{
			float AvgUVScale = TotalEdgeLen / TotalEdgeUVLen;
			UVScale *= AvgUVScale;
		}
	}

	for (int UVID : UVs->ElementIndicesItr())
	{
		FVector2f UV;
		UVs->GetElement(UVID, UV);
		UVs->SetElement(UVID, UV*UVScale);
	}
}






void FDynamicMeshEditor::ReverseTriangleOrientations(const TArray<int>& Triangles, bool bInvertNormals)
{
	for (int tid : Triangles)
	{
		Mesh->ReverseTriOrientation(tid);
	}
	if (bInvertNormals)
	{
		InvertTriangleNormals(Triangles);
	}
}


void FDynamicMeshEditor::InvertTriangleNormals(const TArray<int>& Triangles)
{
	// @todo re-use the TBitA

	if (Mesh->HasVertexNormals())
	{
		TBitArray<FDefaultBitArrayAllocator> DoneVertices(false, Mesh->MaxVertexID());
		for (int TriangleID : Triangles)
		{
			FIndex3i Tri = Mesh->GetTriangle(TriangleID);
			for (int j = 0; j < 3; ++j)
			{
				if (DoneVertices[Tri[j]] == false)
				{
					Mesh->SetVertexNormal(Tri[j], -Mesh->GetVertexNormal(Tri[j]));
					DoneVertices[Tri[j]] = true;
				}
			}
		}
	}


	if (Mesh->HasAttributes())
	{
		for (FDynamicMeshNormalOverlay* Normals : Mesh->Attributes()->GetAllNormalLayers())
		{
			TBitArray<FDefaultBitArrayAllocator> DoneNormals(false, Normals->MaxElementID());
			for (int TriangleID : Triangles)
			{
				FIndex3i ElemTri = Normals->GetTriangle(TriangleID);
				for (int j = 0; j < 3; ++j)
				{
					if (DoneNormals[ElemTri[j]] == false)
					{
						Normals->SetElement(ElemTri[j], -Normals->GetElement(ElemTri[j]));
						DoneNormals[ElemTri[j]] = true;
					}
				}
			}
		}
	}
}





void FDynamicMeshEditor::CopyAttributes(int FromTriangleID, int ToTriangleID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	if (Mesh->HasAttributes() == false)
	{
		return;
	}

	
	for (int UVLayerIndex = 0; UVLayerIndex < Mesh->Attributes()->NumUVLayers(); UVLayerIndex++)
	{
		FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerIndex);
		FIndex3i FromElemTri = UVOverlay->GetTriangle(FromTriangleID);
		FIndex3i ToElemTri = UVOverlay->GetTriangle(ToTriangleID);
		for (int j = 0; j < 3; ++j)
		{
			if (FromElemTri[j] != FDynamicMesh3::InvalidID )
			{
				int NewElemID = FindOrCreateDuplicateUV(FromElemTri[j], UVLayerIndex, IndexMaps);
				ToElemTri[j] = NewElemID;
			}
		}
		UVOverlay->SetTriangle(ToTriangleID, ToElemTri);
	}


	int NormalLayerIndex = 0;
	for (FDynamicMeshNormalOverlay* NormalOverlay : Mesh->Attributes()->GetAllNormalLayers())
	{
		FIndex3i FromElemTri = NormalOverlay->GetTriangle(FromTriangleID);
		FIndex3i ToElemTri = NormalOverlay->GetTriangle(ToTriangleID);
		for (int j = 0; j < 3; ++j)
		{
			if (FromElemTri[j] != FDynamicMesh3::InvalidID)
			{
				int NewElemID = FindOrCreateDuplicateNormal(FromElemTri[j], NormalLayerIndex, IndexMaps);
				ToElemTri[j] = NewElemID;
			}
		}
		NormalOverlay->SetTriangle(ToTriangleID, ToElemTri);
		NormalLayerIndex++;
	}

	if (Mesh->Attributes()->HasMaterialID())
	{
		FDynamicMeshMaterialAttribute* MaterialIDs = Mesh->Attributes()->GetMaterialID();
		MaterialIDs->SetValue(ToTriangleID, MaterialIDs->GetValue(FromTriangleID));
	}

}



int FDynamicMeshEditor::FindOrCreateDuplicateUV(int ElementID, int UVLayerIndex, FMeshIndexMappings& IndexMaps)
{
	int NewElementID = IndexMaps.GetNewUV(UVLayerIndex, ElementID);
	if (NewElementID == IndexMaps.InvalidID())
	{
		FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerIndex);

		// need to determine new parent vertex. It should be in the map already!
		int ParentVertexID = UVOverlay->GetParentVertex(ElementID);
		int NewParentVertexID = IndexMaps.GetNewVertex(ParentVertexID);
		check(NewParentVertexID != IndexMaps.InvalidID());

		NewElementID = UVOverlay->AppendElement(
			UVOverlay->GetElement(ElementID), NewParentVertexID);

		IndexMaps.SetUV(UVLayerIndex, ElementID, NewElementID);
	}
	return NewElementID;
}



int FDynamicMeshEditor::FindOrCreateDuplicateNormal(int ElementID, int NormalLayerIndex, FMeshIndexMappings& IndexMaps)
{
	int NewElementID = IndexMaps.GetNewNormal(NormalLayerIndex, ElementID);
	if (NewElementID == IndexMaps.InvalidID())
	{
		FDynamicMeshNormalOverlay* NormalOverlay = Mesh->Attributes()->GetNormalLayer(NormalLayerIndex);

		// need to determine new parent vertex. It should be in the map already!
		int ParentVertexID = NormalOverlay->GetParentVertex(ElementID);
		int NewParentVertexID = IndexMaps.GetNewVertex(ParentVertexID);
		check(NewParentVertexID != IndexMaps.InvalidID());

		NewElementID = NormalOverlay->AppendElement(
			NormalOverlay->GetElement(ElementID), NewParentVertexID);

		IndexMaps.SetNormal(NormalLayerIndex, ElementID, NewElementID);
	}
	return NewElementID;
}



int FDynamicMeshEditor::FindOrCreateDuplicateVertex(int VertexID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	int NewVertexID = IndexMaps.GetNewVertex(VertexID);
	if (NewVertexID == IndexMaps.InvalidID())
	{
		NewVertexID = Mesh->AppendVertex(*Mesh, VertexID);
		IndexMaps.SetVertex(VertexID, NewVertexID);
		ResultOut.NewVertices.Add(NewVertexID);
	}
	return NewVertexID;
}



int FDynamicMeshEditor::FindOrCreateDuplicateGroup(int TriangleID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	int GroupID = Mesh->GetTriangleGroup(TriangleID);
	int NewGroupID = IndexMaps.GetNewGroup(GroupID);
	if (NewGroupID == IndexMaps.InvalidID())
	{
		NewGroupID = Mesh->AllocateTriangleGroup();
		IndexMaps.SetGroup(GroupID, NewGroupID);
		ResultOut.NewGroups.Add(NewGroupID);
	}
	return NewGroupID;
}




void FDynamicMeshEditor::AppendMesh(const FDynamicMesh3* AppendMesh,
	FMeshIndexMappings& IndexMapsOut, 
	TFunction<FVector3d(int, const FVector3d&)> PositionTransform,
	TFunction<FVector3d(int, const FVector3d&)> NormalTransform)
{
	// todo: handle this case by making a copy?
	check(AppendMesh != Mesh);

	IndexMapsOut.Reset();
	IndexMapsOut.Initialize(Mesh);

	FIndexMapi& VertexMap = IndexMapsOut.GetVertexMap();
	VertexMap.Reserve(AppendMesh->VertexCount());
	for (int VertID : AppendMesh->VertexIndicesItr())
	{
		FVector3d Position = AppendMesh->GetVertex(VertID);
		if (PositionTransform != nullptr)
		{
			Position = PositionTransform(VertID, Position);
		}
		int NewVertID = Mesh->AppendVertex(Position);
		VertexMap.Add(VertID, NewVertID);

		if (AppendMesh->HasVertexNormals() && Mesh->HasVertexNormals())
		{
			FVector3f Normal = AppendMesh->GetVertexNormal(VertID);
			if (NormalTransform != nullptr)
			{
				Normal = (FVector3f)NormalTransform(VertID, (FVector3d)Normal);
			}
			Mesh->SetVertexNormal(NewVertID, Normal);
		}

		if (AppendMesh->HasVertexColors() && Mesh->HasVertexColors())
		{
			FVector3f Color = AppendMesh->GetVertexColor(VertID);
			Mesh->SetVertexColor(NewVertID, Color);
		}
	}

	FIndexMapi& TriangleMap = IndexMapsOut.GetTriangleMap();
	bool bAppendGroups = AppendMesh->HasTriangleGroups() && Mesh->HasTriangleGroups();
	FIndexMapi& GroupsMap = IndexMapsOut.GetGroupMap();
	for (int TriID : AppendMesh->TriangleIndicesItr())
	{
		// append trigroup
		int GroupID = FDynamicMesh3::InvalidID;
		if (bAppendGroups)
		{
			GroupID = AppendMesh->GetTriangleGroup(TriID);
			if (GroupID != FDynamicMesh3::InvalidID)
			{
				const int* FoundNewGroupID = GroupsMap.FindTo(GroupID);
				if (FoundNewGroupID == nullptr)
				{
					int NewGroupID = Mesh->AllocateTriangleGroup();
					GroupsMap.Add(GroupID, NewGroupID);
					GroupID = NewGroupID;
				}
				else
				{
					GroupID = *FoundNewGroupID;
				}
			}
		}

		FIndex3i Tri = AppendMesh->GetTriangle(TriID);
		int NewTriID = Mesh->AppendTriangle(VertexMap.GetTo(Tri.A), VertexMap.GetTo(Tri.B), VertexMap.GetTo(Tri.C), GroupID);
		TriangleMap.Add(TriID, NewTriID);
	}


	// @todo support multiple UV/normal layer copying
	// @todo can we have a template fn that does this?

	if (AppendMesh->HasAttributes() && Mesh->HasAttributes())
	{
		const FDynamicMeshNormalOverlay* FromNormals = AppendMesh->Attributes()->PrimaryNormals();
		FDynamicMeshNormalOverlay* ToNormals = Mesh->Attributes()->PrimaryNormals();
		if (FromNormals != nullptr && ToNormals != nullptr)
		{
			FIndexMapi& NormalMap = IndexMapsOut.GetNormalMap(0);
			NormalMap.Reserve(FromNormals->ElementCount());
			AppendNormals(AppendMesh, FromNormals, ToNormals,
				VertexMap, TriangleMap, NormalTransform, NormalMap);
		}


		int NumUVLayers = FMath::Min(Mesh->Attributes()->NumUVLayers(), AppendMesh->Attributes()->NumUVLayers());
		for (int UVLayerIndex = 0; UVLayerIndex < NumUVLayers; UVLayerIndex++)
		{
			const FDynamicMeshUVOverlay* FromUVs = AppendMesh->Attributes()->GetUVLayer(UVLayerIndex);
			FDynamicMeshUVOverlay* ToUVs = Mesh->Attributes()->GetUVLayer(UVLayerIndex);
			if (FromUVs != nullptr && ToUVs != nullptr)
			{
				FIndexMapi& UVMap = IndexMapsOut.GetUVMap(UVLayerIndex);
				UVMap.Reserve(FromUVs->ElementCount());
				AppendUVs(AppendMesh, FromUVs, ToUVs,
					VertexMap, TriangleMap, UVMap);
			}
		}

		if (AppendMesh->Attributes()->HasMaterialID() && Mesh->Attributes()->HasMaterialID())
		{
			const FDynamicMeshMaterialAttribute* FromMaterialIDs = AppendMesh->Attributes()->GetMaterialID();
			FDynamicMeshMaterialAttribute* ToMaterialIDs = Mesh->Attributes()->GetMaterialID();
			for (int TriID : AppendMesh->TriangleIndicesItr())
			{
				ToMaterialIDs->SetValue(TriangleMap.GetTo(TriID), FromMaterialIDs->GetValue(TriID));
			}
		}
	}
}



void FDynamicMeshEditor::AppendNormals(const FDynamicMesh3* AppendMesh, 
	const FDynamicMeshNormalOverlay* FromNormals, FDynamicMeshNormalOverlay* ToNormals,
	const FIndexMapi& VertexMap, const FIndexMapi& TriangleMap,
	TFunction<FVector3d(int, const FVector3d&)> NormalTransform,
	FIndexMapi& NormalMapOut)
{
	// copy over normals
	for (int ElemID : FromNormals->ElementIndicesItr())
	{
		int ParentVertID = FromNormals->GetParentVertex(ElemID);
		FVector3f Normal = FromNormals->GetElement(ElemID);
		if (NormalTransform != nullptr)
		{
			Normal = (FVector3f)NormalTransform(ParentVertID, (FVector3d)Normal);
		}
		int NewElemID = ToNormals->AppendElement(Normal, VertexMap.GetTo(ParentVertID));
		NormalMapOut.Add(ElemID, NewElemID);
	}

	// now set new triangles
	for (int TriID : AppendMesh->TriangleIndicesItr())
	{
		FIndex3i ElemTri = FromNormals->GetTriangle(TriID);
		int NewTriID = TriangleMap.GetTo(TriID);
		for (int j = 0; j < 3; ++j)
		{
			ElemTri[j] = FromNormals->IsElement(ElemTri[j]) ? NormalMapOut.GetTo(ElemTri[j]) : FDynamicMesh3::InvalidID;
		}
		ToNormals->SetTriangle(NewTriID, ElemTri);
	}
}


void FDynamicMeshEditor::AppendUVs(const FDynamicMesh3* AppendMesh,
	const FDynamicMeshUVOverlay* FromUVs, FDynamicMeshUVOverlay* ToUVs,
	const FIndexMapi& VertexMap, const FIndexMapi& TriangleMap,
	FIndexMapi& UVMapOut)
{
	// copy over uv elements
	for (int ElemID : FromUVs->ElementIndicesItr())
	{
		int ParentVertID = FromUVs->GetParentVertex(ElemID);
		FVector2f UV = FromUVs->GetElement(ElemID);
		int NewElemID = ToUVs->AppendElement(UV, VertexMap.GetTo(ParentVertID));
		UVMapOut.Add(ElemID, NewElemID);
	}

	// now set new triangles
	for (int TriID : AppendMesh->TriangleIndicesItr())
	{
		FIndex3i ElemTri = FromUVs->GetTriangle(TriID);
		int NewTriID = TriangleMap.GetTo(TriID);
		for (int j = 0; j < 3; ++j)
		{
			ElemTri[j] = FromUVs->IsElement(ElemTri[j]) ? UVMapOut.GetTo(ElemTri[j]) : FDynamicMesh3::InvalidID;
		}
		ToUVs->SetTriangle(NewTriID, ElemTri);
	}
}












// can these be replaced w/ template function?

// Utility function for ::AppendTriangles()
static int AppendTriangleUVAttribute(const FDynamicMesh3* FromMesh, int FromElementID, FDynamicMesh3* ToMesh, int UVLayerIndex, FMeshIndexMappings& IndexMaps)
{
	int NewElementID = IndexMaps.GetNewUV(UVLayerIndex, FromElementID);
	if (NewElementID == IndexMaps.InvalidID())
	{
		const FDynamicMeshUVOverlay* FromUVOverlay = FromMesh->Attributes()->GetUVLayer(UVLayerIndex);
		FDynamicMeshUVOverlay* ToUVOverlay = ToMesh->Attributes()->GetUVLayer(UVLayerIndex);

		// need to determine new parent vertex. It should be in the map already!
		int ParentVertexID = FromUVOverlay->GetParentVertex(FromElementID);
		int NewParentVertexID = IndexMaps.GetNewVertex(ParentVertexID);
		check(NewParentVertexID != IndexMaps.InvalidID());

		NewElementID = ToUVOverlay->AppendElement(
			FromUVOverlay->GetElement(FromElementID), NewParentVertexID);

		IndexMaps.SetUV(UVLayerIndex, FromElementID, NewElementID);
	}
	return NewElementID;
}


// Utility function for ::AppendTriangles()
static int AppendTriangleNormalAttribute(const FDynamicMesh3* FromMesh, int FromElementID, FDynamicMesh3* ToMesh, int NormalLayerIndex, FMeshIndexMappings& IndexMaps)
{
	int NewElementID = IndexMaps.GetNewNormal(NormalLayerIndex, FromElementID);
	if (NewElementID == IndexMaps.InvalidID())
	{
		const FDynamicMeshNormalOverlay* FromNormalOverlay = FromMesh->Attributes()->GetNormalLayer(NormalLayerIndex);
		FDynamicMeshNormalOverlay* ToNormalOverlay = ToMesh->Attributes()->GetNormalLayer(NormalLayerIndex);

		// need to determine new parent vertex. It should be in the map already!
		int ParentVertexID = FromNormalOverlay->GetParentVertex(FromElementID);
		int NewParentVertexID = IndexMaps.GetNewVertex(ParentVertexID);
		check(NewParentVertexID != IndexMaps.InvalidID());

		NewElementID = ToNormalOverlay->AppendElement(
			FromNormalOverlay->GetElement(FromElementID), NewParentVertexID);

		IndexMaps.SetNormal(NormalLayerIndex, FromElementID, NewElementID);
	}
	return NewElementID;
}




// Utility function for ::AppendTriangles()
static void AppendAttributes(const FDynamicMesh3* FromMesh, int FromTriangleID, FDynamicMesh3* ToMesh, int ToTriangleID, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
{
	if (FromMesh->HasAttributes() == false || ToMesh->HasAttributes() == false)
	{
		return;
	}

	// todo: if we ever support multiple normal layers, copy them all
	check(FromMesh->Attributes()->NumNormalLayers() == 1);

	for (int UVLayerIndex = 0; UVLayerIndex < FMath::Min(FromMesh->Attributes()->NumUVLayers(), ToMesh->Attributes()->NumUVLayers()); UVLayerIndex++)
	{
		const FDynamicMeshUVOverlay* FromUVOverlay = FromMesh->Attributes()->GetUVLayer(UVLayerIndex);
		FDynamicMeshUVOverlay* ToUVOverlay = ToMesh->Attributes()->GetUVLayer(UVLayerIndex);
		FIndex3i FromElemTri = FromUVOverlay->GetTriangle(FromTriangleID);
		FIndex3i ToElemTri = ToUVOverlay->GetTriangle(ToTriangleID);
		for (int j = 0; j < 3; ++j)
		{
			if (FromElemTri[j] != FDynamicMesh3::InvalidID)
			{
				int NewElemID = AppendTriangleUVAttribute(FromMesh, FromElemTri[j], ToMesh, UVLayerIndex, IndexMaps);
				ToElemTri[j] = NewElemID;
			}
		}
		ToUVOverlay->SetTriangle(ToTriangleID, ToElemTri);
	}


	const FDynamicMeshNormalOverlay* FromNormalOverlay = FromMesh->Attributes()->PrimaryNormals();
	FDynamicMeshNormalOverlay* ToNormalOverlay = ToMesh->Attributes()->PrimaryNormals();

	{
		FIndex3i FromElemTri = FromNormalOverlay->GetTriangle(FromTriangleID);
		FIndex3i ToElemTri = ToNormalOverlay->GetTriangle(ToTriangleID);
		for (int j = 0; j < 3; ++j)
		{
			if (FromElemTri[j] != FDynamicMesh3::InvalidID)
			{
				int NewElemID = AppendTriangleNormalAttribute(FromMesh, FromElemTri[j], ToMesh, 0, IndexMaps);
				ToElemTri[j] = NewElemID;
			}
		}
		ToNormalOverlay->SetTriangle(ToTriangleID, ToElemTri);
	}

	if (FromMesh->Attributes()->HasMaterialID() && ToMesh->Attributes()->HasMaterialID())
	{
		const FDynamicMeshMaterialAttribute* FromMaterialIDs = FromMesh->Attributes()->GetMaterialID();
		FDynamicMeshMaterialAttribute* ToMaterialIDs = ToMesh->Attributes()->GetMaterialID();
		ToMaterialIDs->SetValue(ToTriangleID, FromMaterialIDs->GetValue(FromTriangleID));
	}
}






void FDynamicMeshEditor::AppendTriangles(const FDynamicMesh3* SourceMesh, const TArrayView<const int>& SourceTriangles, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut, bool bComputeTriangleMap)
{
	ResultOut.Reset();
	IndexMaps.Initialize(Mesh);

	for (int SourceTriangleID : SourceTriangles)
	{
		check(SourceMesh->IsTriangle(SourceTriangleID));
		if (SourceMesh->IsTriangle(SourceTriangleID) == false)
		{
			continue;	// ignore missing triangles
		}

		FIndex3i Tri = SourceMesh->GetTriangle(SourceTriangleID);

		// FindOrCreateDuplicateGroup
		int SourceGroupID = SourceMesh->GetTriangleGroup(SourceTriangleID);
		int NewGroupID = IndexMaps.GetNewGroup(SourceGroupID);
		if (NewGroupID == IndexMaps.InvalidID())
		{
			NewGroupID = Mesh->AllocateTriangleGroup();
			IndexMaps.SetGroup(SourceGroupID, NewGroupID);
			ResultOut.NewGroups.Add(NewGroupID);
		}

		// FindOrCreateDuplicateVertex
		FIndex3i NewTri;
		for (int j = 0; j < 3; ++j)
		{
			int SourceVertexID = Tri[j];
			int NewVertexID = IndexMaps.GetNewVertex(SourceVertexID);
			if (NewVertexID == IndexMaps.InvalidID())
			{
				NewVertexID = Mesh->AppendVertex(*SourceMesh, SourceVertexID);
				IndexMaps.SetVertex(SourceVertexID, NewVertexID);
				ResultOut.NewVertices.Add(NewVertexID);
			}
			NewTri[j] = NewVertexID;
		}

		int NewTriangleID = Mesh->AppendTriangle(NewTri, NewGroupID);
		if (bComputeTriangleMap)
		{
			IndexMaps.SetTriangle(SourceTriangleID, NewTriangleID);
		}
		ResultOut.NewTriangles.Add(NewTriangleID);

		AppendAttributes(SourceMesh, SourceTriangleID, Mesh, NewTriangleID, IndexMaps, ResultOut);

		//Mesh->CheckValidity(true);
	}
}


bool FDynamicMeshEditor::SplitMesh(const FDynamicMesh3* SourceMesh, TArray<FDynamicMesh3>& SplitMeshes, TFunctionRef<int(int)> TriIDToMeshID, int DeleteMeshID)
{
	TMap<int, int> MeshIDToIndex;
	int NumMeshes = 0;
	bool bAlsoDelete = false;
	for (int TID : SourceMesh->TriangleIndicesItr())
	{
		int MeshID = TriIDToMeshID(TID);
		if (MeshID == DeleteMeshID)
		{
			bAlsoDelete = true;
			continue;
		}
		if (!MeshIDToIndex.Contains(MeshID))
		{
			MeshIDToIndex.Add(MeshID, NumMeshes++);
		}
	}

	if (!bAlsoDelete && NumMeshes < 2)
	{
		return false; // nothing to do, so don't bother filling the split meshes array
	}

	SplitMeshes.Reset();
	SplitMeshes.SetNum(NumMeshes);
	// enable matching attributes
	for (FDynamicMesh3& M : SplitMeshes)
	{
		if (SourceMesh->HasAttributes())
		{
			M.EnableAttributes();
			M.Attributes()->EnableMatchingAttributes(*SourceMesh->Attributes());
		}
	}

	if (NumMeshes == 0) // full delete case, just leave the empty mesh
	{
		return true;
	}

	TArray<FMeshIndexMappings> Mappings; Mappings.Reserve(NumMeshes);
	FDynamicMeshEditResult UnusedInvalidResultAccumulator; // only here because some functions require it
	for (int Idx = 0; Idx < NumMeshes; Idx++)
	{
		FMeshIndexMappings& Map = Mappings.Emplace_GetRef();
		Map.Initialize(&SplitMeshes[Idx]);
	}

	for (int SourceTID : SourceMesh->TriangleIndicesItr())
	{
		int MeshID = TriIDToMeshID(SourceTID);
		if (MeshID == DeleteMeshID)
		{
			continue; // just skip triangles w/ the Delete Mesh ID
		}
		int MeshIndex = MeshIDToIndex[MeshID];
		FDynamicMesh3& Mesh = SplitMeshes[MeshIndex];
		FMeshIndexMappings& IndexMaps = Mappings[MeshIndex];

		FIndex3i Tri = SourceMesh->GetTriangle(SourceTID);

		// FindOrCreateDuplicateGroup
		int SourceGID = SourceMesh->GetTriangleGroup(SourceTID);
		int NewGID = IndexMaps.GetNewGroup(SourceGID);

		FIndex3i NewTri;
		for (int j = 0; j < 3; ++j)
		{
			int SourceVID = Tri[j];
			int NewVID = IndexMaps.GetNewVertex(SourceVID);
			if (NewVID == IndexMaps.InvalidID())
			{
				NewVID = Mesh.AppendVertex(*SourceMesh, SourceVID);
				IndexMaps.SetVertex(SourceVID, NewVID);
			}
			NewTri[j] = NewVID;
		}

		int NewTID = Mesh.AppendTriangle(NewTri, NewGID);
		IndexMaps.SetTriangle(SourceTID, NewTID);
		AppendAttributes(SourceMesh, SourceTID, &Mesh, NewTID, IndexMaps, UnusedInvalidResultAccumulator);
	}
	
	return true;
}