// Copyright Epic Games, Inc. All Rights Reserved.


#include "Parameterization/DynamicMeshUVEditor.h"
#include "DynamicSubmesh3.h"

#include "DynamicMesh/MeshNormals.h"
#include "MeshBoundaryLoops.h"
#include "DynamicMesh/MeshIndexUtil.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "MeshQueries.h"
#include "MeshWeights.h"

#include "Parameterization/MeshDijkstra.h"
#include "Parameterization/MeshLocalParam.h"
#include "Solvers/MeshParameterizationSolvers.h"

#include "Async/ParallelFor.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

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


void FDynamicMeshUVEditor::ResetUVs()
{
	if (ensure(UVOverlay))
	{
		UVOverlay->ClearElements();
	}
}



void FDynamicMeshUVEditor::TransformUVElements(const TArray<int32>& ElementIDs, TFunctionRef<FVector2f(const FVector2f&)> TransformFunc)
{
	for (int32 elemid : ElementIDs)
	{
		if (UVOverlay->IsElement(elemid))
		{
			FVector2f UV = UVOverlay->GetElement(elemid);
			UV = TransformFunc(UV);
			UVOverlay->SetElement(elemid, UV);
		}
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
	SetTriangleUVsFromPlanarProjection(Triangles, [](const FVector3d& P) { return P; },  
		ProjectionFrame, FVector2d::One(), Result);
}

void FDynamicMeshUVEditor::SetTriangleUVsFromPlanarProjection(
	const TArray<int32>& Triangles, 
	TFunctionRef<FVector3d(const FVector3d&)> PointTransform,
	const FFrame3d& ProjectionFrame, 
	const FVector2d& Dimensions, 
	FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return;
	if (!Triangles.Num()) return;

	double ScaleX = (FMathd::Abs(Dimensions.X) > FMathf::ZeroTolerance) ? (1.0 / Dimensions.X) : 1.0;
	double ScaleY = (FMathd::Abs(Dimensions.Y) > FMathf::ZeroTolerance) ? (1.0 / Dimensions.Y) : 1.0;

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
				FVector3d Pos = Mesh->GetVertex(BaseTri[j]);
				FVector3d TransformPos = PointTransform(Pos);
				FVector2f UV = (FVector2f)ProjectionFrame.ToPlaneUV(TransformPos, 2);
				UV.X *= ScaleX;
				UV.Y *= ScaleY;
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


bool FDynamicMeshUVEditor::EstimateGeodesicCenterFrameVertex(const FDynamicMesh3& Mesh, FFrame3d& FrameOut, int32& VertexIDOut, bool bAlignToUnitAxes)
{
	VertexIDOut = *Mesh.VertexIndicesItr().begin();
	FVector3d Normal = FMeshNormals::ComputeVertexNormal(Mesh, VertexIDOut);

	FMeshBoundaryLoops LoopsCalc(&Mesh, true);
	if (ensure(LoopsCalc.GetLoopCount() > 0) == false )
	{
		FrameOut = Mesh.GetVertexFrame(VertexIDOut, false, &Normal);
		return false;
	}
	const FEdgeLoop& Loop = LoopsCalc[LoopsCalc.GetMaxVerticesLoopIndex()];
	TArray<FVector2d> SeedPoints;
	for (int32 vid : Loop.Vertices)
	{
		SeedPoints.Add(FVector2d(vid, 0.0));
	}

	TMeshDijkstra<FDynamicMesh3> Dijkstra(&Mesh);
	Dijkstra.ComputeToMaxDistance(SeedPoints, TNumericLimits<float>::Max());
	int32 MaxDistVID = Dijkstra.GetMaxGraphDistancePointID();
	if ( ensure(Mesh.IsVertex(MaxDistVID)) == false )
	{
		FrameOut = Mesh.GetVertexFrame(VertexIDOut, false, &Normal);
		return false;
	}
	VertexIDOut = MaxDistVID;
	Normal = FMeshNormals::ComputeVertexNormal(Mesh, MaxDistVID);
	FrameOut = Mesh.GetVertexFrame(MaxDistVID, false, &Normal);

	// try to generate consistent frame alignment...
	if (bAlignToUnitAxes)
	{
		FrameOut.ConstrainedAlignPerpAxes(0, 1, 2, FVector3d::UnitX(), FVector3d::UnitY(), 0.95);
	}

	return true;
}


bool FDynamicMeshUVEditor::EstimateGeodesicCenterFrameVertex(const FDynamicMesh3& Mesh, const TArray<int32>& Triangles, FFrame3d& FrameOut, int32& VertexIDOut, bool bAlignToUnitAxes)
{
	FDynamicSubmesh3 SubmeshCalc(&Mesh, Triangles, (int)EMeshComponents::None, false);
	FDynamicMesh3& Submesh = SubmeshCalc.GetSubmesh();
	FFrame3d SeedFrame;
	int32 FrameVertexID;
	bool bFrameOK = EstimateGeodesicCenterFrameVertex(Submesh, SeedFrame, FrameVertexID, true);
	if (!bFrameOK)
	{
		return false;
	}
	FrameVertexID = SubmeshCalc.MapVertexToBaseMesh(FrameVertexID);
	return true;
}


bool FDynamicMeshUVEditor::SetTriangleUVsFromExpMap(const TArray<int32>& Triangles, FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return false;
	if (Triangles.Num() == 0) return false;

	FDynamicSubmesh3 SubmeshCalc(Mesh, Triangles, (int)EMeshComponents::None, false);
	FDynamicMesh3& Submesh = SubmeshCalc.GetSubmesh();
	FMeshNormals::QuickComputeVertexNormals(Submesh);

	FFrame3d SeedFrame;
	int32 FrameVertexID;
	bool bFrameOK = EstimateGeodesicCenterFrameVertex(Submesh, SeedFrame, FrameVertexID, true);
	if (!bFrameOK)
	{
		return false;
	}

	TMeshLocalParam<FDynamicMesh3> Param(&Submesh);
	Param.ParamMode = ELocalParamTypes::ExponentialMapUpwindAvg;
	Param.ComputeToMaxDistance(FrameVertexID, SeedFrame, TNumericLimits<float>::Max());

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




bool FDynamicMeshUVEditor::SetTriangleUVsFromExpMap(
	const TArray<int32>& Triangles,
	TFunctionRef<FVector3d(const FVector3d&)> PointTransform,
	const FFrame3d& ProjectionFrame,
	const FVector2d& Dimensions,
	int32 NormalSmoothingRounds,
	double NormalSmoothingAlpha,
	double FrameNormalBlendWeight,
	FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return false;
	if (Triangles.Num() == 0) return false;

	double ScaleX = (FMathd::Abs(Dimensions.X) > FMathf::ZeroTolerance) ? (1.0 / Dimensions.X) : 1.0;
	double ScaleY = (FMathd::Abs(Dimensions.Y) > FMathf::ZeroTolerance) ? (1.0 / Dimensions.Y) : 1.0;

	FDynamicSubmesh3 SubmeshCalc(Mesh, Triangles, (int)EMeshComponents::None, false);
	FDynamicMesh3& Submesh = SubmeshCalc.GetSubmesh();
	MeshTransforms::ApplyTransform(Submesh, PointTransform, [&](const FVector3f& V) { return V;});
	FMeshNormals::QuickComputeVertexNormals(Submesh);

	NormalSmoothingRounds = FMath::Clamp(NormalSmoothingRounds, 0, 500);
	NormalSmoothingAlpha = FMathd::Clamp(NormalSmoothingAlpha, 0.0, 1.0);
	if (NormalSmoothingRounds > 0 && NormalSmoothingAlpha > 0)
	{
		int32 NumV = Submesh.MaxVertexID();
		TArray<FVector3d> SmoothedNormals;
		SmoothedNormals.SetNum(NumV);
		for (int32 ri = 0; ri < NormalSmoothingRounds; ++ri)
		{
			SmoothedNormals.Init(FVector3d::Zero(), NumV);
			for (int32 vid : Submesh.VertexIndicesItr())
			{
				FVector3d SmoothedNormal = FVector3d::Zero();
				UE::Geometry::FMeshWeights::CotanWeightsBlendSafe(Submesh, vid, [&](int32 nbrvid, double Weight)
				{
					SmoothedNormal += Weight * (FVector3d)Submesh.GetVertexNormal(nbrvid);
				});
				SmoothedNormal.Normalize();
				SmoothedNormals[vid] = Lerp((FVector3d)Submesh.GetVertexNormal(vid), SmoothedNormal, NormalSmoothingAlpha);
			}
			for (int32 vid : Submesh.VertexIndicesItr())
			{
				Submesh.SetVertexNormal(vid, (FVector3f)SmoothedNormals[vid]);
			}
		}
	}

	FDynamicMeshAABBTree3 Spatial(&Submesh, true);
	double NearDistSqr;
	int32 SeedTID = Spatial.FindNearestTriangle(ProjectionFrame.Origin, NearDistSqr);
	FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(Submesh, SeedTID, ProjectionFrame.Origin);
	FIndex3i TriVerts = Submesh.GetTriangle(SeedTID);

	FFrame3d ParamSeedFrame = ProjectionFrame;
	ParamSeedFrame.Origin = Query.ClosestTrianglePoint;
	// correct for inverted frame
	if (ParamSeedFrame.Z().Dot(Submesh.GetTriNormal(SeedTID)) < 0)
	{
		ParamSeedFrame.Rotate(FQuaterniond(ParamSeedFrame.X(), 180.0, true));
	}

	// apply normal blending
	FrameNormalBlendWeight = FMathd::Clamp(FrameNormalBlendWeight, 0, 1);
	if (FrameNormalBlendWeight > 0)
	{
		FVector3d FrameZ = ParamSeedFrame.Z();
		for (int32 vid : Submesh.VertexIndicesItr())
		{
			FVector3d N = (FVector3d)Submesh.GetVertexNormal(vid);
			N = Lerp(N, FrameZ, FrameNormalBlendWeight);
			Submesh.SetVertexNormal(vid, (FVector3f)N);
		}
	}

	TMeshLocalParam<FDynamicMesh3> Param(&Submesh);
	Param.ParamMode = ELocalParamTypes::ExponentialMapUpwindAvg;
	Param.ComputeToMaxDistance(ParamSeedFrame, TriVerts, TNumericLimits<float>::Max());

	TArray<int32> VtxElementIDs;
	TArray<int32> NewElementIDs;
	VtxElementIDs.Init(IndexConstants::InvalidID, Submesh.MaxVertexID());
	for (int32 vid : Submesh.VertexIndicesItr())
	{
		if (Param.HasUV(vid))
		{
			FVector2f UV = (FVector2f)Param.GetUV(vid);
			UV.X *= ScaleX;
			UV.Y *= ScaleY;
			VtxElementIDs[vid] = UVOverlay->AppendElement(UV);
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
			double DistSqr = DistanceSquared(Submesh.GetVertex(ConstrainLoop[i]), Submesh.GetVertex(ConstrainLoop[j]));
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
			FIndex3i NextTri = UE::Geometry::FindNextAdjacentTriangleAroundVtx(Mesh, SplitVtx, CurTri, PrevTri,
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
			FIndex3i NextTri = UE::Geometry::FindNextAdjacentTriangleAroundVtx(Mesh, SplitVtx, CurTri, PrevTri,
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




void FDynamicMeshUVEditor::SetTriangleUVsFromBoxProjection(
	const TArray<int32>& Triangles, 
	TFunctionRef<FVector3d(const FVector3d&)> PointTransform, 
	const FFrame3d& BoxFrame, 
	const FVector3d& BoxDimensions, 
	FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return;
	int32 NumTriangles = Triangles.Num();
	if (!NumTriangles) return;

	const int Minor1s[3] = { 1, 0, 0 };
	const int Minor2s[3] = { 2, 2, 1 };
	const int Minor1Flip[3] = { -1, 1, 1 };
	const int Minor2Flip[3] = { -1, -1, 1 };

	auto GetTriNormal = [this, &PointTransform](int32 tid) -> FVector3d
	{
		FVector3d A, B, C;
		Mesh->GetTriVertices(tid, A, B, C);
		return VectorUtil::Normal(PointTransform(A), PointTransform(B), PointTransform(C));
	};

	double ScaleX = (FMathd::Abs(BoxDimensions.X) > FMathf::ZeroTolerance) ? (1.0 / BoxDimensions.X) : 1.0;
	double ScaleY = (FMathd::Abs(BoxDimensions.Y) > FMathf::ZeroTolerance) ? (1.0 / BoxDimensions.Y) : 1.0;
	double ScaleZ = (FMathd::Abs(BoxDimensions.Z) > FMathf::ZeroTolerance) ? (1.0 / BoxDimensions.Z) : 1.0;
	FVector3d Scale(ScaleX, ScaleY, ScaleZ);

	TArray<FVector3d> TriNormals;
	TArray<FIndex2i> TriangleBoxPlaneAssignments;
	TriNormals.SetNum(NumTriangles);
	TriangleBoxPlaneAssignments.SetNum(NumTriangles);
	ParallelFor(NumTriangles, [&](int32 i)
	{
		int32 tid = Triangles[i];
		TriNormals[i] = GetTriNormal(tid);
		FVector3d N = BoxFrame.ToFrameVector(TriNormals[i]);
		N *= Scale;
		FVector3d NAbs(FMathd::Abs(N.X), FMathd::Abs(N.Y), FMathd::Abs(N.Z));
		int MajorAxis = NAbs[0] > NAbs[1] ? (NAbs[0] > NAbs[2] ? 0 : 2) : (NAbs[1] > NAbs[2] ? 1 : 2);
		double MajorAxisSign = FMathd::Sign(N[MajorAxis]);
		int Bucket = (MajorAxisSign > 0) ? (MajorAxis+3) : MajorAxis;
		TriangleBoxPlaneAssignments[i] = FIndex2i(MajorAxis, Bucket);
	});

	auto ProjAxis = [](const FVector3d& P, int Axis1, int Axis2, float Axis1Scale, float Axis2Scale)
	{
		return FVector2f(float(P[Axis1]) * Axis1Scale, float(P[Axis2]) * Axis2Scale);
	};

	TMap<FIndex2i, int32> BaseToOverlayVIDMap;
	TArray<int32> NewUVIndices;

	for ( int32 i = 0; i < NumTriangles; ++i )
	{
		int32 tid = Triangles[i];
		FIndex3i BaseTri = Mesh->GetTriangle(tid);
		FIndex2i TriBoxInfo = TriangleBoxPlaneAssignments[i];
		FVector3d N = BoxFrame.ToFrameVector(TriNormals[i]);

		int MajorAxis = TriBoxInfo.A;
		int Bucket = TriBoxInfo.B;
		double MajorAxisSign = FMathd::Sign(N[MajorAxis]);
		int Minor1 = Minor1s[MajorAxis];
		int Minor2 = Minor2s[MajorAxis];

		FIndex3i ElemTri;
		for (int32 j = 0; j < 3; ++j)
		{
			FIndex2i ElementKey(BaseTri[j], Bucket);
			const int32* FoundElementID = BaseToOverlayVIDMap.Find(ElementKey);
			if (FoundElementID == nullptr)
			{
				FVector3d Pos = Mesh->GetVertex(BaseTri[j]);
				FVector3d TransformPos = PointTransform(Pos);
				FVector3d BoxPos = BoxFrame.ToFramePoint(TransformPos);
				BoxPos *= Scale;

				FVector2f UV = ProjAxis(BoxPos, Minor1, Minor2, MajorAxisSign * Minor1Flip[MajorAxis], Minor2Flip[MajorAxis]);

				ElemTri[j] = UVOverlay->AppendElement(UV);
				NewUVIndices.Add(ElemTri[j]);
				BaseToOverlayVIDMap.Add(ElementKey, ElemTri[j]);
			}
			else
			{
				ElemTri[j] = *FoundElementID;
			}
		}
		UVOverlay->SetTriangle(tid, ElemTri);
	}

	if (Result != nullptr)
	{
		Result->NewUVElements = MoveTemp(NewUVIndices);
	}
}






void FDynamicMeshUVEditor::SetTriangleUVsFromCylinderProjection(
	const TArray<int32>& Triangles, 
	TFunctionRef<FVector3d(const FVector3d&)> PointTransform, 
	const FFrame3d& BoxFrame, 
	const FVector3d& BoxDimensions, 
	float CylinderSplitAngle,
	FUVEditResult* Result)
{
	if (ensure(UVOverlay) == false) return;
	int32 NumTriangles = Triangles.Num();
	if (!NumTriangles) return;

	const int Minor1s[3] = { 1, 0, 0 };
	const int Minor2s[3] = { 2, 2, 1 };
	const int Minor1Flip[3] = { -1, 1, 1 };
	const int Minor2Flip[3] = { -1, -1, 1 };

	auto GetTriNormalCentroid = [this, &PointTransform](int32 tid) -> TPair<FVector3d,FVector3d>
	{
		FVector3d A, B, C;
		Mesh->GetTriVertices(tid, A, B, C);
		A = PointTransform(A); B = PointTransform(B); C = PointTransform(C);
		return TPair<FVector3d, FVector3d>{ VectorUtil::Normal(A, B, C), (A + B + C) / 3.0};
	};

	double ScaleX = (FMathd::Abs(BoxDimensions.X) > FMathf::ZeroTolerance) ? (1.0 / BoxDimensions.X) : 1.0;
	double ScaleY = (FMathd::Abs(BoxDimensions.Y) > FMathf::ZeroTolerance) ? (1.0 / BoxDimensions.Y) : 1.0;
	double ScaleZ = (FMathd::Abs(BoxDimensions.Z) > FMathf::ZeroTolerance) ? (1.0 / BoxDimensions.Z) : 1.0;
	FVector3d Scale(ScaleX, ScaleY, ScaleZ);

	double DotThresholdRejectFromPlane = FMathd::Cos(CylinderSplitAngle * FMathf::DegToRad);

	// sort triangles into buckets based on normal. 1/0 is +/-Z, and 3/4 is negative/positive angle around the cylinder,
	// where angles range from [-180,180]. Currently we split at 0 so the 3=[-180,0] and 4=[0,180] spans get their own UV islands
	TArray<FVector3d> TriNormals;
	TArray<FIndex2i> TriangleCylinderAssignments;
	TriNormals.SetNum(NumTriangles);
	TriangleCylinderAssignments.SetNum(NumTriangles);
	ParallelFor(NumTriangles, [&](int32 i)
	{
		int32 tid = Triangles[i];
		TPair<FVector3d, FVector3d> NormalCentroid = GetTriNormalCentroid(tid);
		TriNormals[i] = NormalCentroid.Key;
		FVector3d N = BoxFrame.ToFrameVector(TriNormals[i]);
		N = Normalized(N * Scale);

		if (FMathd::Abs(N.Z) > DotThresholdRejectFromPlane)
		{
			int MajorAxis = 2;
			double MajorAxisSign = FMathd::Sign(N[MajorAxis]);
			// project to +/- Z
			int Bucket = MajorAxisSign > 0 ? 1 : 0;

			TriangleCylinderAssignments[i] = FIndex2i(MajorAxis, Bucket);
		}
		else
		{
			FVector3d Centroid = BoxFrame.ToFramePoint(NormalCentroid.Value);
			double CentroidAngle = FMathd::Atan2(Centroid.Y, Centroid.X);
			int Bucket = (CentroidAngle < 0) ? 3 : 4;
			TriangleCylinderAssignments[i] = FIndex2i(-1, Bucket);
		}
	});

	auto ProjAxis = [](const FVector3d& P, int Axis1, int Axis2, float Axis1Scale, float Axis2Scale)
	{
		return FVector2f(float(P[Axis1]) * Axis1Scale, float(P[Axis2]) * Axis2Scale);
	};

	TMap<FIndex2i, int32> BaseToOverlayVIDMap;
	TArray<int32> NewUVIndices;

	for ( int32 i = 0; i < NumTriangles; ++i )
	{
		int32 tid = Triangles[i];
		FIndex3i BaseTri = Mesh->GetTriangle(tid);
		FIndex2i TriBoxInfo = TriangleCylinderAssignments[i];
		FVector3d N = BoxFrame.ToFrameVector(TriNormals[i]);

		int MajorAxis = TriBoxInfo.A;
		int Bucket = TriBoxInfo.B;

		FIndex3i ElemTri;
		for (int32 j = 0; j < 3; ++j)
		{
			FIndex2i ElementKey(BaseTri[j], Bucket);
			const int32* FoundElementID = BaseToOverlayVIDMap.Find(ElementKey);
			if (FoundElementID == nullptr)
			{
				FVector3d TransPos = PointTransform(Mesh->GetVertex(BaseTri[j]));
				FVector3d BoxPos = Scale * BoxFrame.ToFramePoint(TransPos);

				FVector2f UV = FVector2f::Zero();
				if (Bucket <= 2)
				{
					UV = ProjAxis(BoxPos, 0, 1, FMathd::Sign(N[MajorAxis]) * Minor1Flip[MajorAxis], Minor2Flip[MajorAxis]);
				}
				else
				{
					double VAngle = FMathd::Atan2(BoxPos.Y, BoxPos.X);
					if (Bucket == 4 && VAngle < -FMathd::HalfPi)				// 4 = [0, 180]
					{
						VAngle += FMathd::TwoPi;
					}
					else if (Bucket == 3 && VAngle > FMathd::HalfPi)		// 3=[-180,0]
					{
						VAngle -= FMathd::TwoPi;
					}
					UV = FVector2f( -(float(VAngle) * FMathf::InvPi - 1.0f), -float(BoxPos.Z));
				}

				ElemTri[j] = UVOverlay->AppendElement(UV);
				NewUVIndices.Add(ElemTri[j]);
				BaseToOverlayVIDMap.Add(ElementKey, ElemTri[j]);
			}
			else
			{
				ElemTri[j] = *FoundElementID;
			}
		}

		UVOverlay->SetTriangle(tid, ElemTri);
	}

	if (Result != nullptr)
	{
		Result->NewUVElements = MoveTemp(NewUVIndices);
	}
}