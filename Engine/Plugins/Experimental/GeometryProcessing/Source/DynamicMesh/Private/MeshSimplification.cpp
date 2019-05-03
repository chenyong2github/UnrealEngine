// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshSimplification.h"
#include "DynamicMeshAttributeSet.h"
#include "Util/IndexUtil.h"




void FMeshSimplification::InitializeVertexQuadrics()
{
	int NT = Mesh->MaxTriangleID();
	TArray<FQuadricErrord> triQuadrics;
	triQuadrics.SetNum(NT);
	TArray<double> triAreas;
	triAreas.SetNum(NT);
	//@todo parallel version
	//gParallel.BlockStartEnd(0, Mesh->MaxTriangleID - 1, (start_tid, end_tid) = > {
	FVector3d c, n;
	for (int tid : Mesh->TriangleIndicesItr())
	{
		Mesh->GetTriInfo(tid, n, triAreas[tid], c);
		triQuadrics[tid] = FQuadricErrord(n, c);
	}


	int NV = Mesh->MaxVertexID();
	vertQuadrics.SetNum(NV);
	//gParallel.BlockStartEnd(0, Mesh->MaxVertexID - 1, (start_vid, end_vid) = > {
	for (int vid : Mesh->VertexIndicesItr())
	{
		vertQuadrics[vid] = FQuadricErrord::Zero();
		for (int tid : Mesh->VtxTrianglesItr(vid))
		{
			vertQuadrics[vid].Add(triAreas[tid], triQuadrics[tid]);
		}
		//check(TMathUtil.EpsilonEqual(0, vertQuadrics[i].Evaluate(Mesh->GetVertex(i)), TMathUtil.Epsilon * 10));
	}
}



void FMeshSimplification::InitializeQueue()
{
	int NE = Mesh->EdgeCount();
	int MaxEID = Mesh->MaxEdgeID();

	EdgeQuadrics.SetNum(MaxEID);
	EdgeQueue.Initialize(MaxEID);
	TArray<FEdgeError> EdgeErrors;
	EdgeErrors.SetNum(MaxEID);

	// @todo vertex quadrics can be computed in parallel
	//gParallel.BlockStartEnd(0, MaxEID - 1, (start_eid, end_eid) = > {
	//for (int eid = start_eid; eid <= end_eid; eid++) {
	for (int eid : Mesh->EdgeIndicesItr())
	{
		FIndex2i ev = Mesh->GetEdgeV(eid);
		FQuadricErrord Q(vertQuadrics[ev.A], vertQuadrics[ev.B]);
		FVector3d opt = OptimalPoint(eid, Q, ev.A, ev.B);
		EdgeErrors[eid] = { (float)Q.Evaluate(opt), eid };
		EdgeQuadrics[eid] = QEdge(eid, Q, opt);
	}

	// sorted pq insert is faster, so sort edge errors array and index map
	EdgeErrors.Sort();

	// now do inserts
	int N = EdgeErrors.Num();
	for (int i = 0; i < N; ++i) 
	{
		int eid = EdgeErrors[i].eid;
		if (Mesh->IsEdge(eid))
		{
			QEdge edge = EdgeQuadrics[eid];
			EdgeQueue.Insert(edge.eid, EdgeErrors[i].error);
		}
	}

	/*
	// previous code that does unsorted insert. This is marginally slower, but
	// might get even slower on larger meshes? have only tried up to about 350k.
	// (still, this function is not the bottleneck...)
	int cur_eid = StartEdges();
	bool done = false;
	do {
		if (Mesh->IsEdge(cur_eid)) {
			QEdge edge = EdgeQuadrics[cur_eid];
			double err = errList[cur_eid];
			EdgeQueue.Enqueue(cur_eid, (float)err);
		}
		cur_eid = GetNextEdge(cur_eid, out done);
	} while (done == false);
	*/
}





FVector3d FMeshSimplification::OptimalPoint(int eid, const FQuadricErrord& q, int ea, int eb)
{
	// if we would like to preserve boundary, we need to know that here
	// so that we properly score these edges
	if (bHaveBoundary && bPreserveBoundaryShape)
	{
		if (Mesh->IsBoundaryEdge(eid))
		{
			return (Mesh->GetVertex(ea) + Mesh->GetVertex(eb)) * 0.5;
		}
		else
		{
			if (IsBoundaryVertex(ea))
			{
				return Mesh->GetVertex(ea);
			}
			else if (IsBoundaryVertex(eb))
			{
				return Mesh->GetVertex(eb);
			}
		}
	}

	// [TODO] if we have constraints, we should apply them here, for same reason as bdry above...

	if (bMinimizeQuadricPositionError == false)
	{
		return GetProjectedPoint((Mesh->GetVertex(ea) + Mesh->GetVertex(eb)) * 0.5);
	}
	else
	{
		FVector3d result = FVector3d::Zero();
		if (q.OptimalPoint(result))
		{
			return GetProjectedPoint(result);
		}

		// degenerate matrix, evaluate quadric at edge end and midpoints
		// (could do line search here...)
		FVector3d va = Mesh->GetVertex(ea);
		FVector3d vb = Mesh->GetVertex(eb);
		FVector3d c = GetProjectedPoint((va + vb) * 0.5);
		double fa = q.Evaluate(va);
		double fb = q.Evaluate(vb);
		double fc = q.Evaluate(c);
		double m = FMath::Min3(fa, fb, fc);
		if (m == fa)
		{
			return va;
		}
		else if (m == fb)
		{
			return vb;
		}
		return c;
	}
}




// update queue weight for each edge in vertex one-ring
void FMeshSimplification::UpdateNeighbours(int vid)
{
	for (int eid : Mesh->VtxEdgesItr(vid))
	{
		FIndex2i nev = Mesh->GetEdgeV(eid);
		FQuadricErrord Q(vertQuadrics[nev.A], vertQuadrics[nev.B]);
		FVector3d opt = OptimalPoint(eid, Q, nev.A, nev.B);
		double err = Q.Evaluate(opt);
		EdgeQuadrics[eid] = QEdge(eid, Q, opt);
		if (EdgeQueue.Contains(eid))
		{
			EdgeQueue.Update(eid, (float)err);
		}
		else
		{
			EdgeQueue.Insert(eid, (float)err);
		}
	}
}




void FMeshSimplification::Precompute(bool bMeshIsClosed)
{
	bHaveBoundary = false;
	IsBoundaryVtxCache.SetNum(Mesh->MaxVertexID());
	if (bMeshIsClosed == false)
	{
		for (int eid : Mesh->BoundaryEdgeIndicesItr())
		{
			FIndex2i ev = Mesh->GetEdgeV(eid);
			IsBoundaryVtxCache[ev.A] = true;
			IsBoundaryVtxCache[ev.B] = true;
			bHaveBoundary = true;
		}
	}
}




void FMeshSimplification::DoSimplify()
{
	if (Mesh->TriangleCount() == 0)    // badness if we don't catch this...
	{
		return;
	}

	ProfileBeginPass();

	ProfileBeginSetup();
	Precompute();
	if (Cancelled())
	{
		return;
	}
	InitializeVertexQuadrics();
	if (Cancelled())
	{
		return;
	}
	InitializeQueue();
	if (Cancelled())
	{
		return;
	}
	ProfileEndSetup();

	ProfileBeginOps();

	ProfileBeginCollapse();
	while (EdgeQueue.GetCount() > 0) 
	{
		// termination criteria
		if (SimplifyMode == ETargetModes::VertexCount)
		{
			if (Mesh->VertexCount() <= TargetCount)
			{
				break;
			}
		}
		else 
		{
			if (Mesh->TriangleCount() <= TargetCount)
			{
				break;
			}
		}

		COUNT_ITERATIONS++;
		int eid = EdgeQueue.Dequeue();
		if (Mesh->IsEdge(eid) == false)
		{
			continue;
		}
		if (Cancelled())
		{
			return;
		}

		int vKeptID;
		EProcessResult result = CollapseEdge(eid, EdgeQuadrics[eid].collapse_pt, vKeptID);
		if (result == EProcessResult::Ok_Collapsed) 
		{
			vertQuadrics[vKeptID] = EdgeQuadrics[eid].q;
			UpdateNeighbours(vKeptID);
		}
	}
	ProfileEndCollapse();
	ProfileEndOps();

	if (Cancelled())
	{
		return;
	}

	Reproject();

	ProfileEndPass();
}


void FMeshSimplification::SimplifyToTriangleCount(int nCount)
{
	SimplifyMode = ETargetModes::TriangleCount;
	TargetCount = FMath::Max(1, nCount);
	MinEdgeLength = FMathd::MaxReal;
	DoSimplify();
}

void FMeshSimplification::SimplifyToVertexCount(int nCount)
{
	SimplifyMode = ETargetModes::VertexCount;
	TargetCount = FMath::Max(3, nCount);
	MinEdgeLength = FMathd::MaxReal;
	DoSimplify();
}

void FMeshSimplification::SimplifyToEdgeLength(double minEdgeLen)
{
	SimplifyMode = ETargetModes::MinEdgeLength;
	TargetCount = 1;
	MinEdgeLength = minEdgeLen;
	DoSimplify();
}




void FMeshSimplification::FastCollapsePass(double fMinEdgeLength, int nRounds, bool MeshIsClosedHint)
{
	if (Mesh->TriangleCount() == 0)    // badness if we don't catch this...
	{
		return;
	}

	MinEdgeLength = fMinEdgeLength;
	double min_sqr = MinEdgeLength * MinEdgeLength;

	// we don't collapse on the boundary
	bHaveBoundary = false;

	ProfileBeginPass();

	ProfileBeginSetup();
	Precompute(MeshIsClosedHint);
	if (Cancelled())
	{
		return;
	}
	ProfileEndSetup();

	ProfileBeginOps();

	ProfileBeginCollapse();

	int N = Mesh->MaxEdgeID();
	int num_last_pass = 0;
	for (int ri = 0; ri < nRounds; ++ri) 
	{
		num_last_pass = 0;

		FVector3d va = FVector3d::Zero(), vb = FVector3d::Zero();
		for (int eid = 0; eid < N; ++eid) 
		{
			if ( ( ! Mesh->IsEdge(eid) ) || Mesh->IsBoundaryEdge(eid) )
			{
				continue;
			}
			if (Cancelled())
			{
				return;
			}

			Mesh->GetEdgeV(eid, va, vb);
			if (va.DistanceSquared(vb) > min_sqr)
			{
				continue;
			}

			COUNT_ITERATIONS++;

			FVector3d midpoint = (va + vb) * 0.5;
			int vKeptID;
			EProcessResult result = CollapseEdge(eid, midpoint, vKeptID);
			if (result == EProcessResult::Ok_Collapsed) 
			{
				++num_last_pass;
			}
		}

		if (num_last_pass == 0)     // converged
		{
			break;
		}
	}
	ProfileEndCollapse();
	ProfileEndOps();

	if (Cancelled())
	{
		return;
	}

	Reproject();

	ProfileEndPass();
}














FMeshSimplification::EProcessResult FMeshSimplification::CollapseEdge(int edgeID, FVector3d vNewPos, int& collapseToV)
{
	collapseToV = FDynamicMesh3::InvalidID;
	RuntimeDebugCheck(edgeID);

	FEdgeConstraint constraint =
		(Constraints == nullptr) ? FEdgeConstraint::Unconstrained() : Constraints->GetEdgeConstraint(edgeID);
	if (constraint.NoModifications())
	{
		return EProcessResult::Ignored_EdgeIsFullyConstrained;
	}
	if (constraint.CanCollapse() == false)
	{
		return EProcessResult::Ignored_EdgeIsFullyConstrained;
	}


	// look up verts and tris for this edge
	if (Mesh->IsEdge(edgeID) == false)
	{
		return EProcessResult::Failed_NotAnEdge;
	}
	FIndex4i edgeInfo = Mesh->GetEdge(edgeID);
	int a = edgeInfo.A, b = edgeInfo.B, t0 = edgeInfo.C, t1 = edgeInfo.D;
	bool bIsBoundaryEdge = (t1 == FDynamicMesh3::InvalidID);

	// look up 'other' verts c (from t0) and d (from t1, if it exists)
	FIndex3i T0tv = Mesh->GetTriangle(t0);
	int c = IndexUtil::FindTriOtherVtx(a, b, T0tv);
	FIndex3i T1tv = (bIsBoundaryEdge) ? FDynamicMesh3::InvalidTriangle() : Mesh->GetTriangle(t1);
	int d = (bIsBoundaryEdge) ? FDynamicMesh3::InvalidID : IndexUtil::FindTriOtherVtx(a, b, T1tv);

	FVector3d vA = Mesh->GetVertex(a);
	FVector3d vB = Mesh->GetVertex(b);
	double edge_len_sqr = (vA - vB).SquaredLength();
	if (edge_len_sqr > MinEdgeLength * MinEdgeLength)
	{
		return EProcessResult::Ignored_EdgeTooLong;
	}

	ProfileBeginCollapse();

	// check if we should collapse, and also find which vertex we should collapse to,
	// in cases where we have constraints/etc
	int collapse_to = -1;
	bool bCanCollapse = CanCollapseEdge(edgeID, a, b, c, d, t0, t1, collapse_to);
	if (bCanCollapse == false)
	{
		return EProcessResult::Ignored_Constrained;
	}

	// if we have a boundary, we want to collapse to boundary
	if (bPreserveBoundaryShape && bHaveBoundary) 
	{
		if (collapse_to != -1) 
		{
			if ((IsBoundaryVertex(b) && collapse_to != b) ||
				(IsBoundaryVertex(a) && collapse_to != a))
			{
				return EProcessResult::Ignored_Constrained;
			}
		}
		if (IsBoundaryVertex(b))
		{
			collapse_to = b;
		}
		else if (IsBoundaryVertex(a))
		{
			collapse_to = a;
		}
	}

	// optimization: if edge cd exists, we cannot collapse or flip. look that up here?
	//  funcs will do it internally...
	//  (or maybe we can collapse if cd exists? edge-collapse doesn't check for it explicitly...)
	EProcessResult retVal = EProcessResult::Failed_OpNotSuccessful;

	int iKeep = b, iCollapse = a;

	// if either vtx is fixed, collapse to that position
	double collapse_t = 0;
	if (collapse_to == b) 
	{
		vNewPos = vB;
		collapse_t = 0;
	}
	else if (collapse_to == a) 
	{
		iKeep = a; iCollapse = b;
		vNewPos = vA;
		collapse_t = 0;
	}
	else
	{
		vNewPos = GetProjectedCollapsePosition(iKeep, vNewPos);
		double div = vA.Distance(vB);
		collapse_t = (div < FMathd::ZeroTolerance) ? 0.5 : (vNewPos.Distance(Mesh->GetVertex(iKeep))) / div;
		collapse_t = VectorUtil::Clamp(collapse_t, 0.0, 1.0);
	}

	// check if this collapse will create a normal flip. Also checks
	// for invalid collapse nbrhood, since we are doing one-ring iter anyway.
	// [TODO] could we skip this one-ring check in CollapseEdge? pass in hints?
	if (CheckIfCollapseCreatesFlipOrInvalid(a, b, vNewPos, t0, t1) || CheckIfCollapseCreatesFlipOrInvalid(b, a, vNewPos, t0, t1)) 
	{
		ProfileEndCollapse();
		return EProcessResult::Ignored_CreatesFlip;
	}

	// lots of cases where we cannot collapse, but we should just let
	// Mesh sort that out, right?
	COUNT_COLLAPSES++;
	FDynamicMesh3::FEdgeCollapseInfo collapseInfo;
	EMeshResult result = Mesh->CollapseEdge(iKeep, iCollapse, collapse_t, collapseInfo);
	if (result == EMeshResult::Ok) 
	{
		collapseToV = iKeep;
		Mesh->SetVertex(iKeep, vNewPos);
		if (Constraints != nullptr) 
		{
			Constraints->ClearEdgeConstraint(edgeID);
			Constraints->ClearEdgeConstraint(collapseInfo.RemovedEdges.A);
			if (collapseInfo.RemovedEdges.B != FDynamicMesh3::InvalidID)
			{
				Constraints->ClearEdgeConstraint(collapseInfo.RemovedEdges.B);
			}
			Constraints->ClearVertexConstraint(iCollapse);
		}
		OnEdgeCollapse(edgeID, iKeep, iCollapse, collapseInfo);
		DoDebugChecks();

		retVal = EProcessResult::Ok_Collapsed;
	}

	ProfileEndCollapse();
	return retVal;
}








// Project vertices onto projection target. 
// We can do projection in parallel if we have .net 
void FMeshSimplification::FullProjectionPass()
{
	auto project = [&](int vID) 
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

	ApplyToProjectVertices(project);

	// [RMS] not sure how to do this...
	//if (EnableParallelProjection) {
	//    gParallel.ForEach<int>(project_vertices(), project);
	//} else {
	//    foreach (int vid in project_vertices())
	//        project(vid);
	//}
}


void FMeshSimplification::ApplyToProjectVertices(const TFunction<void(int)>& apply_f)
{
	for (int vid : Mesh->VertexIndicesItr())
	{
		apply_f(vid);
	}
}


void FMeshSimplification::ProjectVertex(int vID, IProjectionTarget* targetIn)
{
	FVector3d curpos = Mesh->GetVertex(vID);
	FVector3d projected = targetIn->Project(curpos, vID);
	Mesh->SetVertex(vID, projected);
}

// used by collapse-edge to get projected position for new vertex
FVector3d FMeshSimplification::GetProjectedCollapsePosition(int vid, const FVector3d& vNewPos)
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

