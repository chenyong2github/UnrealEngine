// Copyright Epic Games, Inc. All Rights Reserved.


#include "Parameterization/DynamicMeshUVEditor.h"
#include "DynamicSubmesh3.h"

#include "MeshNormals.h"
#include "MeshBoundaryLoops.h"
#include "MeshIndexUtil.h"

#include "Parameterization/MeshDijkstra.h"
#include "Parameterization/MeshLocalParam.h"
#include "Solvers/MeshParameterizationSolvers.h"


FDynamicMeshUVEditor::FDynamicMeshUVEditor(FDynamicMesh3* MeshIn, int32 UVLayerIndex, bool bCreateIfMissing)
{
	Mesh = MeshIn;

	if (Mesh->HasAttributes() && Mesh->Attributes()->NumUVLayers() > UVLayerIndex)
	{
		UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerIndex);
	}

	if (UVOverlay == nullptr && bCreateIfMissing)
	{
		CreateUVLayer(UVLayerIndex);
		UVOverlay = Mesh->Attributes()->GetUVLayer(UVLayerIndex);
		check(UVOverlay);
	}
}



void FDynamicMeshUVEditor::CreateUVLayer(int32 LayerIndex)
{
	if (Mesh->HasAttributes() == false)
	{
		Mesh->EnableAttributes();
	}

	if (Mesh->Attributes()->NumUVLayers() <= LayerIndex)
	{
		Mesh->Attributes()->SetNumUVLayers(LayerIndex+1);
	}
}



template<typename EnumerableType>
void InternalSetPerTriangleUVs(EnumerableType TriangleIDs, const FDynamicMesh3* Mesh, FDynamicMeshUVOverlay* UVOverlay, double ScaleFactor, FUVEditResult* Result)
{
	TMap<int32, int32> BaseToOverlayVIDMap;
	TArray<int32> NewUVIndices;

	for (int32 TriangleID : TriangleIDs)
	{
		FIndex3i MeshTri = Mesh->GetTriangle(TriangleID);
		FFrame3d TriProjFrame = Mesh->GetTriFrame(TriangleID, 0);

		FIndex3i ElemTri;
		for (int32 j = 0; j < 3; ++j)
		{
			FVector2f UV = (FVector2f)TriProjFrame.ToPlaneUV(Mesh->GetVertex(MeshTri[j]), 2);
			UV *= ScaleFactor;
			ElemTri[j] = UVOverlay->AppendElement(UV);
			NewUVIndices.Add(ElemTri[j]);
		}
		UVOverlay->SetTriangle(TriangleID, ElemTri);
	}

	if (Result != nullptr)
	{
		Result->NewUVElements = MoveTemp(NewUVIndices);
	}
}


void FDynamicMeshUVEditor::SetPerTriangleUVs(const TArray<int32>& Triangles, double ScaleFactor, FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return;
	if (!Triangles.Num()) return;

	InternalSetPerTriangleUVs(Triangles, Mesh, UVOverlay, ScaleFactor, Result);
}


void FDynamicMeshUVEditor::SetPerTriangleUVs(double ScaleFactor, FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return;
	if (Mesh->TriangleCount() <= 0) return;

	InternalSetPerTriangleUVs(Mesh->TriangleIndicesItr(), Mesh, UVOverlay, ScaleFactor, Result);
}


void FDynamicMeshUVEditor::SetTriangleUVsFromProjection(const TArray<int32>& Triangles, const FFrame3d& ProjectionFrame, FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return;
	if (!Triangles.Num()) return;

	TMap<int32, int32> BaseToOverlayVIDMap;
	TArray<int32> NewUVIndices;

	for (int32 TID : Triangles)
	{
		FIndex3i BaseTri = Mesh->GetTriangle(TID);
		FIndex3i ElemTri;
		for (int32 j = 0; j < 3; ++j)
		{
			const int32* FoundElementID = BaseToOverlayVIDMap.Find(BaseTri[j]);
			if (FoundElementID == nullptr)
			{
				FVector2f UV = (FVector2f)ProjectionFrame.ToPlaneUV(Mesh->GetVertex(BaseTri[j]), 2);
				ElemTri[j] = UVOverlay->AppendElement(UV);
				NewUVIndices.Add(ElemTri[j]);
				BaseToOverlayVIDMap.Add(BaseTri[j], ElemTri[j]);
			}
			else
			{
				ElemTri[j] = *FoundElementID;
			}
		}
		UVOverlay->SetTriangle(TID, ElemTri);
	}

	if (Result != nullptr)
	{
		Result->NewUVElements = MoveTemp(NewUVIndices);
	}
}



bool FDynamicMeshUVEditor::SetTriangleUVsFromExpMap(const TArray<int32>& Triangles, FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return false;
	if (Triangles.Num() == 0) return false;

	FDynamicSubmesh3 SubmeshCalc(Mesh, Triangles, (int)EMeshComponents::None, false);
	FDynamicMesh3& Submesh = SubmeshCalc.GetSubmesh();
	FMeshNormals::QuickComputeVertexNormals(Submesh);

	FMeshBoundaryLoops LoopsCalc(&Submesh, true);
	if (LoopsCalc.GetLoopCount() == 0)
	{
		return false;
	}
	const FEdgeLoop& Loop = LoopsCalc[LoopsCalc.GetMaxVerticesLoopIndex()];
	TArray<FVector2d> SeedPoints;
	for (int32 vid : Loop.Vertices)
	{
		SeedPoints.Add(FVector2d(vid, 0.0));
	}

	TMeshDijkstra<FDynamicMesh3> Dijkstra(&Submesh);
	Dijkstra.ComputeToMaxDistance(SeedPoints, TNumericLimits<float>::Max());
	int32 MaxDistVID = Dijkstra.GetMaxGraphDistancePointID();
	if (!Submesh.IsVertex(MaxDistVID))
	{
		return false;
	}


	FFrame3d SeedFrame(Submesh.GetVertex(MaxDistVID), (FVector3d)Submesh.GetVertexNormal(MaxDistVID));
	// try to generate consistent frame alignment...
	SeedFrame.ConstrainedAlignPerpAxes(0, 1, 2, FVector3d::UnitX(), FVector3d::UnitY(), 0.95);

	TMeshLocalParam<FDynamicMesh3> Param(&Submesh);
	Param.ParamMode = ELocalParamTypes::ExponentialMapUpwindAvg;
	Param.ComputeToMaxDistance(MaxDistVID, SeedFrame, TNumericLimits<float>::Max());

	TArray<int32> VtxElementIDs;
	TArray<int32> NewElementIDs;
	VtxElementIDs.Init(IndexConstants::InvalidID, Submesh.MaxVertexID());
	for (int32 vid : Submesh.VertexIndicesItr())
	{
		if (Param.HasUV(vid))
		{
			VtxElementIDs[vid] = UVOverlay->AppendElement( (FVector2f)Param.GetUV(vid) );
			NewElementIDs.Add(VtxElementIDs[vid]);
		}
	}

	int32 NumFailed = 0;
	for (int32 tid : Submesh.TriangleIndicesItr())
	{
		FIndex3i SubTri = Submesh.GetTriangle(tid);
		FIndex3i UVTri = { VtxElementIDs[SubTri.A], VtxElementIDs[SubTri.B], VtxElementIDs[SubTri.C] };
		if (UVTri.A == IndexConstants::InvalidID || UVTri.B == IndexConstants::InvalidID || UVTri.C == IndexConstants::InvalidID)
		{
			NumFailed++;
			continue;
		}

		int32 BaseTID = SubmeshCalc.MapTriangleToBaseMesh(tid);
		UVOverlay->SetTriangle(BaseTID, UVTri);
	}

	if (Result != nullptr)
	{
		Result->NewUVElements = MoveTemp(NewElementIDs);
	}

	return (NumFailed == 0);
}






bool FDynamicMeshUVEditor::SetTriangleUVsFromFreeBoundaryConformal(const TArray<int32>& Triangles, FUVEditResult* Result)
{
	return SetTriangleUVsFromFreeBoundaryConformal(Triangles, false, Result);
}
bool FDynamicMeshUVEditor::SetTriangleUVsFromFreeBoundaryConformal(const TArray<int32>& Triangles, bool bUseExistingUVTopology, FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return false;
	if (Triangles.Num() == 0) return false;

	FDynamicMesh3 Submesh(EMeshComponents::None);
	TMap<int32, int32> BaseToSubmeshV;
	TArray<int32> SubmeshToBaseV;
	TArray<int32> SubmeshToBaseT;

	for (int32 tid : Triangles)
	{
		FIndex3i Triangle = (bUseExistingUVTopology) ? UVOverlay->GetTriangle(tid) : Mesh->GetTriangle(tid);
		FIndex3i NewTriangle;
		for (int32 j = 0; j < 3; ++j)
		{
			const int32* FoundIdx = BaseToSubmeshV.Find(Triangle[j]);
			if (FoundIdx)
			{
				NewTriangle[j] = *FoundIdx;
			}
			else
			{
				FVector3d Position = Mesh->GetVertex((bUseExistingUVTopology) ? UVOverlay->GetParentVertex(Triangle[j]) : Triangle[j]);
				int32 NewVtxID = Submesh.AppendVertex(Position);
				check(NewVtxID == SubmeshToBaseV.Num());
				SubmeshToBaseV.Add(NewVtxID);
				BaseToSubmeshV.Add(Triangle[j], NewVtxID);
				NewTriangle[j] = NewVtxID;
			}
		}

		int32 NewTriID = Submesh.AppendTriangle(NewTriangle);
		check(NewTriID == SubmeshToBaseT.Num());
		SubmeshToBaseT.Add(tid);
	}

	// is there a quick check we can do to ensure that we have a single connected component?

	// make the UV solver
	TUniquePtr<UE::Solvers::IConstrainedMeshUVSolver> Solver = UE::MeshDeformation::ConstructNaturalConformalParamSolver(Submesh);

	// find pair of vertices to constrain. The standard procedure is to find the two furthest-apart vertices on the
	// largest boundary loop. 
	FMeshBoundaryLoops Loops(&Submesh, true);
	if (Loops.GetLoopCount() == 0) return false;
	const TArray<int32>& ConstrainLoop = Loops[Loops.GetLongestLoopIndex()].Vertices;
	int32 LoopNum = ConstrainLoop.Num();
	FIndex2i MaxDistPair = FIndex2i::Invalid();
	double MaxDistSqr = 0;
	for (int32 i = 0; i < LoopNum; ++i)
	{
		for (int32 j = i + 1; j < LoopNum; ++j)
		{
			double DistSqr = Submesh.GetVertex(ConstrainLoop[i]).DistanceSquared(Submesh.GetVertex(ConstrainLoop[j]));
			if (DistSqr > MaxDistSqr)
			{
				MaxDistSqr = DistSqr;
				MaxDistPair = FIndex2i(i, j);
			}
		}
	}
	if (ensure(MaxDistPair != FIndex2i::Invalid()) == false)
	{
		return false;
	}

	// pin those vertices
	Solver->AddConstraint(MaxDistPair.A, 1.0, FVector2d(0.0, 0.5), false);
	Solver->AddConstraint(MaxDistPair.B, 1.0, FVector2d(1.0, 0.5), false);

	// solve for UVs
	TArray<FVector2d> UVBuffer;
	if (Solver->SolveUVs(&Submesh, UVBuffer) == false)
	{
		return false;
	}


	int32 NumFailed = 0;
	if (bUseExistingUVTopology)
	{
		// only need to copy elements
		int32 NumSubVerts = SubmeshToBaseV.Num();
		for (int32 k = 0; k < NumSubVerts; ++k )
		{
			FVector2d NewUV = UVBuffer[k];
			int32 ElemID = SubmeshToBaseV[k];
			UVOverlay->SetElement(ElemID, (FVector2f)NewUV);
		}

		if (Result != nullptr)
		{
			Result->NewUVElements = MoveTemp(SubmeshToBaseV);
		}
	}
	else
	{
		// copy back to target UVOverlay
		TArray<int32> VtxElementIDs;
		TArray<int32> NewElementIDs;
		VtxElementIDs.Init(IndexConstants::InvalidID, Submesh.MaxVertexID());
		for (int32 vid : Submesh.VertexIndicesItr())
		{
			VtxElementIDs[vid] = UVOverlay->AppendElement((FVector2f)UVBuffer[vid]);
			NewElementIDs.Add(VtxElementIDs[vid]);
		}

		// set triangles
		for (int32 tid : Submesh.TriangleIndicesItr())
		{
			FIndex3i SubTri = Submesh.GetTriangle(tid);
			FIndex3i UVTri = { VtxElementIDs[SubTri.A], VtxElementIDs[SubTri.B], VtxElementIDs[SubTri.C] };
			if (ensure(UVTri.A != IndexConstants::InvalidID && UVTri.B != IndexConstants::InvalidID && UVTri.C != IndexConstants::InvalidID) == false)
			{
				NumFailed++;
				continue;
			}
			int32 BaseTID = SubmeshToBaseT[tid];
			UVOverlay->SetTriangle(BaseTID, UVTri);
		}

		if (Result != nullptr)
		{
			Result->NewUVElements = MoveTemp(NewElementIDs);
		}
	}

	return (NumFailed == 0);
}







// Assuming that SplitVtx is either on a mesh boundary or UV seam, find the two sets of one-ring triangles
// on either side of edge BaseEdgeID that are edge-connected in the UV overlay. Either set could be empty.
static bool FindSeamTriSplitSets_BoundaryVtx(const FDynamicMesh3* Mesh, const FDynamicMeshUVOverlay* UVOverlay, 
	int32 SplitVtx, 
	int32 BaseEdgeID,
	TArray<int32> SplitTriSets[2])
{
	FIndex2i StartTris = Mesh->GetEdgeT(BaseEdgeID);
	check(Mesh->IsTriangle(StartTris.A) && Mesh->IsTriangle(StartTris.B));

	for (int32 si = 0; si < 2; ++si)
	{
		int32 StartTri = StartTris[si];
		SplitTriSets[si].Add(StartTri);
		
		int32 EdgeOtherTri = StartTris[si==0?1:0];
		int32 CurTri = StartTri;
		int32 PrevTri = EdgeOtherTri;

		bool bDone = false;
		while (!bDone)
		{
			FIndex3i NextTri = UE::MeshIndexUtil::FindNextAdjacentTriangleAroundVtx(Mesh, SplitVtx, CurTri, PrevTri,
				[&](int32 Tri0, int32 Tri1, int32 Edge) { return UVOverlay->AreTrianglesConnected(Tri0, Tri1); }
			);
			if (NextTri.A != IndexConstants::InvalidID)
			{
				if (NextTri.A == EdgeOtherTri)		// if we looped around, SplitVtx is not on a boundary and we need to abort
				{
					return false;
				}

				SplitTriSets[si].Add(NextTri.A);
				PrevTri = CurTri;
				CurTri = NextTri.A;
			}
			else
			{
				bDone = true;
			}
		}
	}

	return true;
}



// If we want to cut the UV one-ring at SplitVtx with the edge sequence [PrevBaseEdgeID,NextBaseEdgeID], assuming both
// edges are connected to SplitVtx, we need to find the connected sets of triangles on either side of NextBaseEdgeID
// (assuming PrevBaseEdgeID was already handled). To do that we walk around the uv-connected-one-ring away from NextBaseEdgeID
// in either direction.
static bool FindSeamTriSplitSets_InteriorVtx(const FDynamicMesh3* Mesh, const FDynamicMeshUVOverlay* UVOverlay, 
	int32 SplitVtx, 
	int32 PrevBaseEdgeID, int32 NextBaseEdgeID,
	TArray<int32> SplitTriSets[2] )
{
	FIndex2i StartTris = Mesh->GetEdgeT(NextBaseEdgeID);
	check(Mesh->IsTriangle(StartTris.A) && Mesh->IsTriangle(StartTris.B));

	for (int32 si = 0; si < 2; ++si)
	{
		int32 StartTri = StartTris[si];
		SplitTriSets[si].Add(StartTri);

		int32 EdgeOtherTri = StartTris[si == 0 ? 1 : 0];
		int32 CurTri = StartTri;
		int32 PrevTri = EdgeOtherTri;

		bool bDone = false;
		while (!bDone)
		{
			FIndex3i NextTri = UE::MeshIndexUtil::FindNextAdjacentTriangleAroundVtx(Mesh, SplitVtx, CurTri, PrevTri,
				[&](int32 Tri0, int32 Tri1, int32 Edge) { return UVOverlay->AreTrianglesConnected(Tri0, Tri1) && Edge != PrevBaseEdgeID && Edge != NextBaseEdgeID; }
			);
			if (NextTri.A != IndexConstants::InvalidID)
			{
				if (NextTri.A == EdgeOtherTri)		// if we somehow looped around, then the arguments were bad and we need to abort
				{
					return false;
				}

				SplitTriSets[si].Add(NextTri.A);
				PrevTri = CurTri;
				CurTri = NextTri.A;
			}
			else
			{
				bDone = true;
			}
		}
	}

	return true;
}


// Find the UV element for MeshVertexID that is contained in the "first" triangle of MeshEdgeID
static int32 FindUVElementForVertex(const FDynamicMesh3* Mesh, const FDynamicMeshUVOverlay* UVOverlay, int32 MeshVertexID, int32 MeshEdgeID)
{
	FIndex2i Tris = Mesh->GetEdgeT(MeshEdgeID);
	FIndex3i Tri0 = Mesh->GetTriangle(Tris.A);
	FIndex3i UVTri0 = UVOverlay->GetTriangle(Tris.A);
	for (int32 j = 0; j < 3; ++j)
	{
		if (Tri0[j] == MeshVertexID)
		{
			return UVTri0[j];
		}
	}
	return IndexConstants::InvalidID;
}



bool FDynamicMeshUVEditor::CreateSeamAlongVertexPath(const TArray<int32>& VertexPath, FUVEditResult* Result)
{
	// todo: handle paths that are loops (may just work?)
	check(VertexPath[0] != VertexPath.Last());

	// construct list of sequential mesh edges, and set of seam edges, for VertexPath
	TArray<int32> EdgePath;
	TSet<int32> SeamEdges;
	int32 NumVerts = VertexPath.Num();
	for (int32 k = 0; k < NumVerts - 1; ++k)
	{
		int32 FoundEdgeID = Mesh->FindEdge(VertexPath[k], VertexPath[k+1]);
		if (Mesh->IsEdge(FoundEdgeID) == false)
		{
			return false;
		}

		EdgePath.Add(FoundEdgeID);
		if (UVOverlay->IsSeamEdge(FoundEdgeID))
		{
			SeamEdges.Add(FoundEdgeID);
		}
	}

	bool bStartIsSeamVtx = UVOverlay->IsSeamVertex(VertexPath[0]);
	bool bEndIsSeamVtx = UVOverlay->IsSeamVertex(VertexPath.Last());

	// handle start vert. If it's on a boundary/seam we split it, otherwise we do not (next-vtx split will create seam)
	if (bStartIsSeamVtx && SeamEdges.Contains(EdgePath[0]) == false)
	{
		TArray<int32> SplitSets[2];
		bool bSetsOK = FindSeamTriSplitSets_BoundaryVtx(Mesh, UVOverlay, VertexPath[0], EdgePath[0], SplitSets);
		check(bSetsOK);
		int32 ElementID = FindUVElementForVertex(Mesh, UVOverlay, VertexPath[0], EdgePath[0]);
		check(ElementID != IndexConstants::InvalidID);
		int32 NewElementID = UVOverlay->SplitElement(ElementID, SplitSets[0]);
		if (Result)
		{
			Result->NewUVElements.Add(NewElementID);
		}
	}

	// handle end-vert similarly
	if (bEndIsSeamVtx && SeamEdges.Contains(EdgePath.Last()) == false)
	{
		TArray<int32> SplitSets[2];
		bool bSetsOK = FindSeamTriSplitSets_BoundaryVtx(Mesh, UVOverlay, VertexPath.Last(), EdgePath.Last(), SplitSets);
		check(bSetsOK);
		int32 ElementID = FindUVElementForVertex(Mesh, UVOverlay, VertexPath.Last(), EdgePath.Last());
		check(ElementID != IndexConstants::InvalidID);
		int32 NewElementID = UVOverlay->SplitElement(ElementID, SplitSets[0]);
		if (Result)
		{
			Result->NewUVElements.Add(NewElementID);
		}
	}

	// walk through intermediate verts and cut connected island at each one
	for (int32 k = 1; k < NumVerts - 1; ++k)
	{
		// todo: need to check here that we have not already separated the two SplitSets?

		int32 PrevEdge = EdgePath[k-1];
		int32 NextEdge = EdgePath[k];

		if (Mesh->IsBoundaryEdge(PrevEdge) || Mesh->IsBoundaryEdge(NextEdge))
		{
			continue;
		}

		TArray<int32> SplitSets[2];
		bool bSetsOK = FindSeamTriSplitSets_InteriorVtx(Mesh, UVOverlay, VertexPath[k], PrevEdge, NextEdge, SplitSets);
		check(bSetsOK);
		int32 ElementID = FindUVElementForVertex(Mesh, UVOverlay, VertexPath[k], PrevEdge);
		check(ElementID != IndexConstants::InvalidID);
		int32 NewElementID = UVOverlay->SplitElement(ElementID, SplitSets[0]);
		if (Result)
		{
			Result->NewUVElements.Add(NewElementID);
		}
	}

	return true;
}
