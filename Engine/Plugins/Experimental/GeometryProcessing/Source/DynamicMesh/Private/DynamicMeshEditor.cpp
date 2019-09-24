// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "DynamicMeshEditor.h"
#include "DynamicMeshAttributeSet.h"
#include "Util/BufferUtil.h"
#include "MeshRegionBoundaryLoops.h"



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
	check(bOK);
	if (!bOK)
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
				FIndexMapi& UVMap = IndexMapsOut.GetUVMap(0);
				UVMap.Reserve(FromUVs->ElementCount());
				AppendUVs(AppendMesh, FromUVs, ToUVs,
					VertexMap, TriangleMap, UVMap);
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
				int NewElemID = AppendTriangleUVAttribute(FromMesh, FromElemTri[j], ToMesh, 0, IndexMaps);
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
}






void FDynamicMeshEditor::AppendTriangles(const FDynamicMesh3* SourceMesh, const TArray<int>& SourceTriangles, FMeshIndexMappings& IndexMaps, FDynamicMeshEditResult& ResultOut)
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
		IndexMaps.SetTriangle(SourceTriangleID, NewTriangleID);
		ResultOut.NewTriangles.Add(NewTriangleID);

		AppendAttributes(SourceMesh, SourceTriangleID, Mesh, NewTriangleID, IndexMaps, ResultOut);

		//Mesh->CheckValidity(true);
	}
}
