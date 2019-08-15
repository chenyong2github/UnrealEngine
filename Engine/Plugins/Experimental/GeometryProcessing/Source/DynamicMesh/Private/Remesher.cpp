// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Remesher.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshWeights.h"




void FRemesher::SetTargetEdgeLength(double fLength)
{
	// from Botsch paper
	//MinEdgeLength = fLength * (4.0/5.0);
	//MaxEdgeLength = fLength * (4.0/3.0);
	// much nicer!! makes sense as when we split, edges are both > min !
	MinEdgeLength = fLength * 0.66;
	MaxEdgeLength = fLength * 1.33;
}



void FRemesher::Precompute()
{
	// if we know Mesh is closed, we can skip is-boundary checks, which makes
	// the flip-valence tests much faster!
	bMeshIsClosed = true;
	for (int eid : Mesh->EdgeIndicesItr()) 
	{
		if (Mesh->IsBoundaryEdge(eid)) 
		{
			bMeshIsClosed = false;
			break;
		}
	}
}





void FRemesher::BasicRemeshPass() 
{
	if (Mesh->TriangleCount() == 0)    // badness if we don't catch this...
	{
		return;
	}

	ProfileBeginPass();

	// Iterate over all edges in the mesh at start of pass.
	// Some may be removed, so we skip those.
	// However, some old eid's may also be re-used, so we will touch
	// some new edges. Can't see how we could efficiently prevent this.
	//
	ProfileBeginOps();

	int cur_eid = StartEdges();
	bool done = false;
	ModifiedEdgesLastPass = 0;
	do 
	{
		if (Mesh->IsEdge(cur_eid)) 
		{
			EProcessResult result = ProcessEdge(cur_eid);
			if (result == EProcessResult::Ok_Collapsed || result == EProcessResult::Ok_Flipped || result == EProcessResult::Ok_Split)
			{
				ModifiedEdgesLastPass++;
			}
		}
		if (Cancelled())        // expensive to check every iter?
		{
			return;
		}
		cur_eid = GetNextEdge(cur_eid, done);
	} while (done == false);
	ProfileEndOps();

	if (Cancelled())
	{
		return;
	}

	ProfileBeginSmooth();
	if (bEnableSmoothing && SmoothSpeedT > 0)
	{
		if (bEnableSmoothInPlace) 
		{
			//FullSmoothPass_InPlace(EnableParallelSmooth);
			check(false);
		}
		else
		{
			FullSmoothPass_Buffer(bEnableParallelSmooth);
		}
		DoDebugChecks();
	}
	ProfileEndSmooth();

	if (Cancelled())
	{
		return;
	}

	ProfileBeginProject();
	if (ProjTarget != nullptr && ProjectionMode == ETargetProjectionMode::AfterRefinement)
	{
		FullProjectionPass();
		DoDebugChecks();
	}
	ProfileEndProject();

	DoDebugChecks(true);

	if (Cancelled())
	{
		return;
	}

	ProfileEndPass();
}





FRemesher::EProcessResult FRemesher::ProcessEdge(int edgeID)
{
	RuntimeDebugCheck(edgeID);

	FEdgeConstraint constraint =
		(Constraints == nullptr) ? FEdgeConstraint::Unconstrained() : Constraints->GetEdgeConstraint(edgeID);
	if (constraint.NoModifications())
	{
		return EProcessResult::Ignored_EdgeIsFullyConstrained;
	}

	// look up verts and tris for this edge
	if (Mesh->IsEdge(edgeID) == false)
	{
		return EProcessResult::Failed_NotAnEdge;
	}
	FIndex4i edge_info = Mesh->GetEdge(edgeID);
	int a = edge_info.A, b = edge_info.B, t0 = edge_info.C, t1 = edge_info.D;
	bool bIsBoundaryEdge = (t1 == IndexConstants::InvalidID);

	// look up 'other' verts c (from t0) and d (from t1, if it exists)
	FIndex2i ov = Mesh->GetEdgeOpposingV(edgeID);
	int c = ov[0], d = ov[1];

	FVector3d vA = Mesh->GetVertex(a);
	FVector3d vB = Mesh->GetVertex(b);
	double edge_len_sqr = (vA - vB).SquaredLength();

	ProfileBeginCollapse();

	// check if we should collapse, and also find which vertex we should collapse to,
	// in cases where we have constraints/etc
	int collapse_to = -1;
	bool bCanCollapse = bEnableCollapses
		&& constraint.CanCollapse()
		&& edge_len_sqr < MinEdgeLength*MinEdgeLength
		&& CanCollapseEdge(edgeID, a, b, c, d, t0, t1, collapse_to);

	// optimization: if edge cd exists, we cannot collapse or flip. look that up here?
	//  funcs will do it internally...
	//  (or maybe we can collapse if cd exists? edge-collapse doesn't check for it explicitly...)

	// if edge length is too short, we want to collapse it
	bool bTriedCollapse = false;
	if (bCanCollapse) 
	{
		int iKeep = b, iCollapse = a;
		double collapse_t = 0.5;		// need to know t-value along edge to update lerpable attributes properly
		FVector3d vNewPos = (vA + vB) * collapse_t;

		// if either vtx is fixed, collapse to that position
		if (collapse_to == b) 
		{
			collapse_t = 0;
			vNewPos = vB;
		}
		else if (collapse_to == a) 
		{
			iKeep = a; iCollapse = b;
			collapse_t = 0;
			vNewPos = vA;
		}
		else
		{
			vNewPos = GetProjectedCollapsePosition(iKeep, vNewPos);
			double div = vA.Distance(vB);
			collapse_t = (div < FMathd::ZeroTolerance) ? 0.5 : (vNewPos.Distance(Mesh->GetVertex(iKeep))) / div;
			collapse_t = VectorUtil::Clamp(collapse_t, 0.0, 1.0);
		}

		// if new position would flip normal of one of the existing triangles
		// either one-ring, don't allow it
		if (bPreventNormalFlips) 
		{
			if (CheckIfCollapseCreatesFlipOrInvalid(a, b, vNewPos, t0, t1) || CheckIfCollapseCreatesFlipOrInvalid(b, a, vNewPos, t0, t1)) 
			{
				goto abort_collapse;
			}
		}

		// lots of cases where we cannot collapse, but we should just let
		// mesh sort that out, right?
		COUNT_COLLAPSES++;
		FDynamicMesh3::FEdgeCollapseInfo collapseInfo;
		EMeshResult result = Mesh->CollapseEdge(iKeep, iCollapse, collapse_t, collapseInfo);
		if (result == EMeshResult::Ok) 
		{
			Mesh->SetVertex(iKeep, vNewPos);
			if (Constraints != nullptr) 
			{
				Constraints->ClearEdgeConstraint(edgeID);
				Constraints->ClearEdgeConstraint(collapseInfo.RemovedEdges.A);
				if (collapseInfo.RemovedEdges.B != IndexConstants::InvalidID)
				{
					Constraints->ClearEdgeConstraint(collapseInfo.RemovedEdges.B);
				}
				Constraints->ClearVertexConstraint(iCollapse);
			}
			OnEdgeCollapse(edgeID, iKeep, iCollapse, collapseInfo);
			DoDebugChecks();

			return EProcessResult::Ok_Collapsed;
		}
		else
		{
			bTriedCollapse = true;
		}
	}
abort_collapse:

	ProfileEndCollapse();
	ProfileBeginFlip();

	// if this is not a boundary edge, maybe we want to flip
	bool bTriedFlip = false;
	if (bEnableFlips && constraint.CanFlip() && bIsBoundaryEdge == false) 
	{
		// can we do this more efficiently somehow?
		bool a_is_boundary_vtx = (bMeshIsClosed) ? false : (bIsBoundaryEdge || Mesh->IsBoundaryVertex(a));
		bool b_is_boundary_vtx = (bMeshIsClosed) ? false : (bIsBoundaryEdge || Mesh->IsBoundaryVertex(b));
		bool c_is_boundary_vtx = (bMeshIsClosed) ? false : Mesh->IsBoundaryVertex(c);
		bool d_is_boundary_vtx = (bMeshIsClosed) ? false : Mesh->IsBoundaryVertex(d);
		int valence_a = Mesh->GetVtxEdgeCount(a), valence_b = Mesh->GetVtxEdgeCount(b);
		int valence_c = Mesh->GetVtxEdgeCount(c), valence_d = Mesh->GetVtxEdgeCount(d);
		int valence_a_target = (a_is_boundary_vtx) ? valence_a : 6;
		int valence_b_target = (b_is_boundary_vtx) ? valence_b : 6;
		int valence_c_target = (c_is_boundary_vtx) ? valence_c : 6;
		int valence_d_target = (d_is_boundary_vtx) ? valence_d : 6;

		// if total valence error improves by flip, we want to do it
		int curr_err = abs(valence_a - valence_a_target) + abs(valence_b - valence_b_target)
			+ abs(valence_c - valence_c_target) + abs(valence_d - valence_d_target);
		int flip_err = abs((valence_a - 1) - valence_a_target) + abs((valence_b - 1) - valence_b_target)
			+ abs((valence_c + 1) - valence_c_target) + abs((valence_d + 1) - valence_d_target);

		bool bTryFlip = flip_err < curr_err;
		if (bTryFlip && bPreventNormalFlips && CheckIfFlipInvertsNormals(a, b, c, d, t0))
		{
			bTryFlip = false;
		}

		if (bTryFlip) 
		{
			FDynamicMesh3::FEdgeFlipInfo flipInfo;
			COUNT_FLIPS++;
			EMeshResult result = Mesh->FlipEdge(edgeID, flipInfo);
			if (result == EMeshResult::Ok) 
			{
				OnEdgeFlip(edgeID, flipInfo);
				DoDebugChecks();
				return EProcessResult::Ok_Flipped;
			}
			else
			{
				bTriedFlip = true;
			}

		}
	}

	ProfileEndFlip();
	ProfileBeginSplit();

	// if edge length is too long, we want to split it
	bool bTriedSplit = false;
	if (bEnableSplits && constraint.CanSplit() && edge_len_sqr > MaxEdgeLength*MaxEdgeLength) 
	{
		FDynamicMesh3::FEdgeSplitInfo SplitInfo;
		COUNT_SPLITS++;
		EMeshResult result = Mesh->SplitEdge(edgeID, SplitInfo);
		if (result == EMeshResult::Ok) 
		{
			UpdateAfterSplit(edgeID, a, b, SplitInfo);
			OnEdgeSplit(edgeID, a, b, SplitInfo);
			DoDebugChecks();
			return EProcessResult::Ok_Split;
		}
		else
		{
			bTriedSplit = true;
		}
	}

	ProfileEndSplit();


	if (bTriedFlip || bTriedSplit || bTriedCollapse)
	{
		return EProcessResult::Failed_OpNotSuccessful;
	}
	else
	{
		return EProcessResult::Ignored_EdgeIsFine;
	}
}






void FRemesher::UpdateAfterSplit(int edgeID, int va, int vb, const FDynamicMesh3::FEdgeSplitInfo& SplitInfo)
{
	bool bPositionFixed = false;
	if (Constraints != nullptr && Constraints->HasEdgeConstraint(edgeID)) 
	{
		// inherit edge constraint
		Constraints->SetOrUpdateEdgeConstraint(SplitInfo.NewEdges.A, Constraints->GetEdgeConstraint(edgeID));

		// [RMS] update vertex constraints. Note that there is some ambiguity here.
		//   Both verts being constrained doesn't inherently mean that the edge is on
		//   a constraint, that's why these checks are only applied if edge is constrained.
		//   But constrained edge doesn't necessarily mean we want to inherit vert constraints!!
		//
		//   although, pretty safe to assume that we would at least disable flips
		//   if both vertices are constrained to same line/curve. So, maybe this makes sense...
		//
		//   (perhaps edge constraint should be explicitly tagged to resolve this ambiguity??)

		// vert inherits Fixed if both orig edge verts Fixed, and both tagged with same SetID
		FVertexConstraint ca = Constraints->GetVertexConstraint(va);
		FVertexConstraint cb = Constraints->GetVertexConstraint(vb);
		if (ca.Fixed && cb.Fixed) 
		{
			int nSetID = (ca.FixedSetID > 0 && ca.FixedSetID == cb.FixedSetID) ?
				ca.FixedSetID : FVertexConstraint::InvalidSetID;
			bool bMovable = ca.Movable && cb.Movable;
			Constraints->SetOrUpdateVertexConstraint(SplitInfo.NewVertex,
				FVertexConstraint(true, bMovable, nSetID));
			bPositionFixed = true;
		}

		// vert inherits Target if:
		//  1) both source verts and edge have same Target, and is same as edge target
		//  2) either vert has same target as edge, and other vert is fixed
		if (ca.Target != nullptr || cb.Target != nullptr) 
		{
			IProjectionTarget* edge_target = Constraints->GetEdgeConstraint(edgeID).Target;
			IProjectionTarget* set_target = nullptr;
			if (ca.Target == cb.Target && ca.Target == edge_target)
			{
				set_target = edge_target;
			}
			else if (ca.Target == edge_target && cb.Fixed)
			{
				set_target = edge_target;
			}
			else if (cb.Target == edge_target && ca.Fixed)
			{
				set_target = edge_target;
			}

			if (set_target != nullptr) 
			{
				Constraints->SetOrUpdateVertexConstraint(SplitInfo.NewVertex,
					FVertexConstraint(set_target));
				ProjectVertex(SplitInfo.NewVertex, set_target);
				bPositionFixed = true;
			}
		}
	}

	if (EnableInlineProjection() && bPositionFixed == false && ProjTarget != nullptr) 
	{
		ProjectVertex(SplitInfo.NewVertex, ProjTarget);
	}
}



void FRemesher::ProjectVertex(int VertexID, IProjectionTarget* UseTarget)
{
	FVector3d curpos = Mesh->GetVertex(VertexID);
	FVector3d projected = UseTarget->Project(curpos, VertexID);
	Mesh->SetVertex(VertexID, projected);
}

// used by collapse-edge to get projected position for new vertex
FVector3d FRemesher::GetProjectedCollapsePosition(int vid, const FVector3d& vNewPos)
{
	if (Constraints != nullptr) 
	{
		FVertexConstraint vc = Constraints->GetVertexConstraint(vid);
		if (vc.Target != nullptr)
		{
			return vc.Target->Project(vNewPos, vid);
		}
		if (vc.Fixed)
		{
			return vNewPos;
		}
	}
	// no constraint applied, so if we have a target surface, project to that
	if (EnableInlineProjection() && ProjTarget != nullptr) 
	{
		if (VertexControlF == nullptr || ((int)VertexControlF(vid) & (int)EVertexControl::NoProject) == 0)
		{
			return ProjTarget->Project(vNewPos, vid);
		}
	}
	return vNewPos;
}




static FVector3d UniformSmooth(const FDynamicMesh3& mesh, int vID, double t)
{
	FVector3d v = mesh.GetVertex(vID);
	FVector3d c;
	mesh.GetVtxOneRingCentroid(vID, c);
	return (1.0 - t)*v + (t)*c;
}


static FVector3d MeanValueSmooth(const FDynamicMesh3& mesh, int vID, double t)
{
	FVector3d v = mesh.GetVertex(vID);
	FVector3d c = FMeshWeights::MeanValueCentroid(mesh, vID);
	return (1.0 - t)*v + (t)*c;
}

static FVector3d CotanSmooth(const FDynamicMesh3& mesh, int vID, double t)
{
	FVector3d v = mesh.GetVertex(vID);
	FVector3d c = FMeshWeights::CotanCentroid(mesh, vID);
	return (1.0 - t)*v + (t)*c;
}

TFunction<FVector3d(const FDynamicMesh3&, int, double)> FRemesher::GetSmoothFunction()
{
	if (CustomSmoothF != nullptr)
	{
		return CustomSmoothF;
	} 
	else if (SmoothType == ESmoothTypes::MeanValue)
	{
		return MeanValueSmooth;
	}
	else if (SmoothType == ESmoothTypes::Cotan)
	{
		return CotanSmooth;
	}
	return UniformSmooth;
}


void FRemesher::FullSmoothPass_Buffer(bool bParallel)
{
	InitializeVertexBufferForPass();

	TFunction<FVector3d(const FDynamicMesh3&, int, double)> UseSmoothFunc = GetSmoothFunction();

	auto SmoothAndUpdateFunc = [&](int vID) 
	{
		bool bModified = false;
		FVector3d vSmoothed = ComputeSmoothedVertexPos(vID, UseSmoothFunc, bModified);
		if (bModified) 
		{
			TempFlagBuffer[vID] = true;
			TempPosBuffer[vID] = vSmoothed;
		}
	};

	//if (bParallel) {
	//    gParallel.ForEach<int>(smooth_vertices(), smooth);
	//} else {
	//    foreach (int vID in smooth_vertices())
	//        smooth(vID);
	//}
	ApplyToSmoothVertices(SmoothAndUpdateFunc);

	ApplyVertexBuffer(bParallel);
}






void FRemesher::InitializeVertexBufferForPass()
{
	if ((int)TempPosBuffer.GetLength() < Mesh->MaxVertexID())
	{
		TempPosBuffer.Resize(Mesh->MaxVertexID() + Mesh->MaxVertexID() / 5);
	}
	if (TempFlagBuffer.Num() < Mesh->MaxVertexID()) 
	{
		TempFlagBuffer.SetNum(2 * Mesh->MaxVertexID());
	}

	TempFlagBuffer.Init(false, TempFlagBuffer.Num());
	//TempFlagBuffer.assign(TempFlagBuffer.size(), false);
}

void FRemesher::ApplyVertexBuffer(bool bParallel)
{
	for (int vid : Mesh->VertexIndicesItr()) 
	{
		if (TempFlagBuffer[vid])
		{
			Mesh->SetVertex(vid, TempPosBuffer[vid]);
		}
	}

	// [TODO] can probably use block-parallel here...
	//if (bParallel) {
	//    gParallel.BlockStartEnd(0, Mesh->MaxVertexID-1, (a,b) => {
	//        for (int vid = a; vid <= b; vid++) {
	//            if (TempFlagBuffer[vid])
	//                Mesh->SetVertex(vid, TempPosBuffer[vid]);
	//        }
	//    });
	//} else {
	//    foreach (int vid in Mesh->VertexIndices()) {
	//        if (TempFlagBuffer[vid])
	//            Mesh->SetVertex(vid, TempPosBuffer[vid]);
	//    }
	//}
}



FVector3d FRemesher::ComputeSmoothedVertexPos(int vID,
	TFunction<FVector3d(const FDynamicMesh3&, int, double)> smoothFunc, bool& bModified)
{
	bModified = false;
	FVertexConstraint vConstraint = FVertexConstraint::Unconstrained();
	GetVertexConstraint(vID, vConstraint);
	if (vConstraint.Fixed && vConstraint.Movable == false)
	{
		return Mesh->GetVertex(vID);
	}
	EVertexControl vControl = (VertexControlF == nullptr) ? EVertexControl::AllowAll : VertexControlF(vID);
	if (((int)vControl & (int)EVertexControl::NoSmooth) != 0)
	{
		return Mesh->GetVertex(vID);
	}

	FVector3d vSmoothed = smoothFunc(*Mesh, vID, SmoothSpeedT);
	check(VectorUtil::IsFinite(vSmoothed));     // this will really catch a lot of bugs...

	// project onto either vtx constraint target, or surface target
	if (vConstraint.Target != nullptr) 
	{
		vSmoothed = vConstraint.Target->Project(vSmoothed, vID);
	}
	else if (EnableInlineProjection() && ProjTarget != nullptr) 
	{
		if (((int)vControl & (int)EVertexControl::NoProject) == 0)
		{
			vSmoothed = ProjTarget->Project(vSmoothed, vID);
		}
	}

	bModified = true;
	return vSmoothed;
}


void FRemesher::ApplyToSmoothVertices(const TFunction<void(int)>& VertexSmoothFunc)
{
	for (int vid : Mesh->VertexIndicesItr())
	{
		VertexSmoothFunc(vid);
	}
}




// Project vertices onto projection target. 
// We can do projection in parallel if we have .net 
void FRemesher::FullProjectionPass()
{
	auto UseProjectionFunc = [&](int vID) 
	{
		if (IsVertexConstrained(vID))
		{
			return;
		}
		if (VertexControlF != nullptr && ((int)VertexControlF(vID) & (int)EVertexControl::NoProject) != 0)
		{
			return;
		}
		FVector3d curpos = Mesh->GetVertex(vID);
		FVector3d projected = ProjTarget->Project(curpos, vID);
		Mesh->SetVertex(vID, projected);
	};

	ApplyToProjectVertices(UseProjectionFunc);

	// [RMS] not sure how to do this...
	//if (EnableParallelProjection) {
	//    gParallel.ForEach<int>(project_vertices(), project);
	//} else {
	//    foreach (int vid in project_vertices())
	//        project(vid);
	//}
}




void FRemesher::ApplyToProjectVertices(const TFunction<void(int)>& VertexProjectFunc)
{
	for (int vid : Mesh->VertexIndicesItr())
	{
		VertexProjectFunc(vid);
	}
}





