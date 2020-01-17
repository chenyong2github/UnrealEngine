// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSimplification.h"
#include "DynamicMeshAttributeSet.h"
#include "Util/IndexUtil.h"



template <typename QuadricErrorType>
QuadricErrorType TMeshSimplification<QuadricErrorType>::ComputeFaceQuadric(const int tid, FVector3d& nface, FVector3d& c, double& Area) const
{
	// compute the new quadric for this tri.
	Mesh->GetTriInfo(tid, nface, Area, c);

	return FQuadricErrorType(nface, c);
}


// Face Quadric Error computation specialized for FAttrBasedQuadricErrord
template<>
FAttrBasedQuadricErrord TMeshSimplification<FAttrBasedQuadricErrord>::ComputeFaceQuadric(const int tid, FVector3d& nface, FVector3d& c, double& Area) const
{
	// compute the new quadric for this tri.
	Mesh->GetTriInfo(tid, nface, Area, c);

	FVector3f n0; FVector3f n1; FVector3f n2;

	if (NormalOverlay != nullptr)
	{
		NormalOverlay->GetTriElements(tid, n0, n1, n2);
	}
	else
	{
		FIndex3i vids = Mesh->GetTriangle(tid);
		n0 = Mesh->GetVertexNormal(vids[0]);
		n1 = Mesh->GetVertexNormal(vids[1]);
		n2 = Mesh->GetVertexNormal(vids[2]);
	}


	FVector3d p0, p1, p2;
	Mesh->GetTriVertices(tid, p0, p1, p2);

	FVector3d n0d(n0.X, n0.Y, n0.Z);
	FVector3d n1d(n1.X, n1.Y, n1.Z);
	FVector3d n2d(n2.X, n2.Y, n2.Z);

	double attrweight = 1.;
	return FQuadricErrorType(p0, p1, p2, n0d, n1d, n2d, nface, c, attrweight);
}

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::InitializeTriQuadrics()
{
	const int NT = Mesh->MaxTriangleID();
	triQuadrics.SetNum(NT);
	triAreas.SetNum(NT);

	// tested with ParallelFor - no measurable benifit
	//@todo parallel version
	//gParallel.BlockStartEnd(0, Mesh->MaxTriangleID - 1, (start_tid, end_tid) = > {
	FVector3d n, c;
	for (int tid : Mesh->TriangleIndicesItr())
	{
		triQuadrics[tid] = ComputeFaceQuadric(tid, n, c, triAreas[tid]);
	}

}

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::InitializeVertexQuadrics()
{

	int NV = Mesh->MaxVertexID();
	vertQuadrics.SetNum(NV);
	// tested with ParallelFor - no measurable benifit 
	//gParallel.BlockStartEnd(0, Mesh->MaxVertexID - 1, (start_vid, end_vid) = > {
	for (int vid : Mesh->VertexIndicesItr())
	{
		vertQuadrics[vid] = FQuadricErrorType::Zero();
		for (int tid : Mesh->VtxTrianglesItr(vid))
		{
			vertQuadrics[vid].Add(triAreas[tid], triQuadrics[tid]);
		}
		//check(TMathUtil.EpsilonEqual(0, vertQuadrics[i].Evaluate(Mesh->GetVertex(i)), TMathUtil.Epsilon * 10));
	}

}



template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::InitializeQueue()
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
		FQuadricErrorType Q(vertQuadrics[ev.A], vertQuadrics[ev.B]);
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




template <typename QuadricErrorType>
FVector3d TMeshSimplification<QuadricErrorType>::OptimalPoint(int eid, const FQuadricErrorType& q, int ea, int eb)
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
template <>
void DYNAMICMESH_API TMeshSimplification<FQuadricErrord>::UpdateNeighbours(int vid, FIndex2i removedTris, FIndex2i opposingVerts)
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

// update queue weight for each edge in vertex one-ring.  Memoryless
template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::UpdateNeighbours(int vid, FIndex2i removedTris, FIndex2i opposingVerts)
{


	// This is the faster version that selectively updates the one-ring
	{

		// compute the change in affected face quadrics, and then propagate 
		// that change to the face adjacent verts.
		FVector3d n, c;
		double NewtriArea;

		// Update the triangle areas and quadrics that will have changed
		for (int tid : Mesh->VtxTrianglesItr(vid))
		{

			const double OldtriArea = triAreas[tid];
			const FQuadricErrorType OldtriQuadric = triQuadrics[tid];


			// compute the new quadric for this tri.
			FQuadricErrorType NewtriQuadric = ComputeFaceQuadric(tid, n, c, NewtriArea);

			// update the arrays that hold the current face area & quadrics
			triAreas[tid] = NewtriArea;
			triQuadrics[tid] = NewtriQuadric;

			FIndex3i tri_vids = Mesh->GetTriangle(tid);

			// update the vert quadrics that are adjacent to vid.
			for (int32 i = 0; i < 3; ++i)
			{
				if (tri_vids[i] == vid) continue;

				// correct the adjacent vertQuadrics
				vertQuadrics[tri_vids[i]].Add(-OldtriArea, OldtriQuadric); // subtract old quadric
				vertQuadrics[tri_vids[i]].Add(NewtriArea, NewtriQuadric); // add new quadric
			}
		}

		// remove the influence of the dead tris from the two verts that were opposing the collapsed edge
		{
			for (int i = 0; i < 2; ++i)
			{
				if (removedTris[i] != FDynamicMesh3::InvalidID)
				{
					const double   oldArea = triAreas[removedTris[i]];
					FQuadricErrorType oldQuadric = triQuadrics[removedTris[i]];

					triAreas[removedTris[i]] = 0.;

					// subtract the quadric from the opposing vert
					vertQuadrics[opposingVerts[i]].Add(-oldArea, oldQuadric);
				}
			}
		}
		// Rebuild the quadric for the vert that was retained during the collapse.
		// NB: in the version with memory this quadric took the value of the edge quadric that collapsed.
		{
			FQuadricErrorType vertQuadric;
			for (int tid : Mesh->VtxTrianglesItr(vid))
			{
				vertQuadric.Add(triAreas[tid], triQuadrics[tid]);
			}
			vertQuadrics[vid] = vertQuadric;
		}
	}

	// Update all the edges
	{
		TArray<int, TInlineAllocator<64>> EdgesToUpdate;
		for (int adjvid : Mesh->VtxVerticesItr(vid))
		{
			for (int eid : Mesh->VtxEdgesItr(adjvid))
			{
				EdgesToUpdate.AddUnique(eid);
			}
		}

		for (int eid : EdgesToUpdate)
		{
			// The volume conservation plane data held in the 
			// vertex quadrics will have duplicates for 
			// the two face adjacent to the edge.

			const FIndex4i edgeData = Mesh->GetEdge(eid);
			FQuadricErrorType Q(vertQuadrics[edgeData[0]], vertQuadrics[edgeData[1]]);

			FVector3d opt = OptimalPoint(eid, Q, edgeData[0], edgeData[1]);
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
}





template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::Precompute(bool bMeshIsClosed)
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




template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::DoSimplify()
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
	InitializeTriQuadrics();
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
		else if (SimplifyMode == ETargetModes::MaxError)
		{
			float qe = EdgeQueue.GetFirstNodePriority();
			if (FMath::Abs(qe) > MaxErrorAllowed)
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

		// find triangles adjacent to the target edge
		// and the verts opposite the edge.
		FIndex2i targetTris = Mesh->GetEdgeT(eid);
		FIndex2i targetVrts = Mesh->GetEdgeOpposingV(eid);


		int vKeptID;
		ESimplificationResult result = CollapseEdge(eid, EdgeQuadrics[eid].collapse_pt, vKeptID);
		if (result == ESimplificationResult::Ok_Collapsed)
		{
			vertQuadrics[vKeptID] = EdgeQuadrics[eid].q;
			UpdateNeighbours(vKeptID, targetTris, targetVrts);
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


template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::SimplifyToTriangleCount(int nCount)
{
	SimplifyMode = ETargetModes::TriangleCount;
	TargetCount = FMath::Max(1, nCount);
	MinEdgeLength = FMathd::MaxReal;
	MaxErrorAllowed = FMathf::MaxReal;
	DoSimplify();
}

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::SimplifyToVertexCount(int nCount)
{
	SimplifyMode = ETargetModes::VertexCount;
	TargetCount = FMath::Max(3, nCount);
	MinEdgeLength = FMathd::MaxReal;
	MaxErrorAllowed = FMathf::MaxReal;
	DoSimplify();
}

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::SimplifyToEdgeLength(double minEdgeLen)
{
	SimplifyMode = ETargetModes::MinEdgeLength;
	TargetCount = 1;
	MinEdgeLength = minEdgeLen;
	MaxErrorAllowed = FMathf::MaxReal;
	DoSimplify();
}

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::SimplifyToMaxError(double MaxError)
{
	SimplifyMode = ETargetModes::MaxError;
	TargetCount = 1;
	MinEdgeLength = FMathd::MaxReal;
	MaxErrorAllowed = MaxError;
	DoSimplify();
}




template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::FastCollapsePass(double fMinEdgeLength, int nRounds, bool MeshIsClosedHint)
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
			if ((!Mesh->IsEdge(eid)) || Mesh->IsBoundaryEdge(eid))
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
			ESimplificationResult result = CollapseEdge(eid, midpoint, vKeptID);
			if (result == ESimplificationResult::Ok_Collapsed)
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














template <typename QuadricErrorType>
ESimplificationResult TMeshSimplification<QuadricErrorType>::CollapseEdge(int edgeID, FVector3d vNewPos, int& collapseToV)
{
	collapseToV = FDynamicMesh3::InvalidID;
	RuntimeDebugCheck(edgeID);

	FEdgeConstraint constraint =
		(Constraints == nullptr) ? FEdgeConstraint::Unconstrained() : Constraints->GetEdgeConstraint(edgeID);
	if (constraint.NoModifications())
	{
		return ESimplificationResult::Ignored_EdgeIsFullyConstrained;
	}
	if (constraint.CanCollapse() == false)
	{
		return ESimplificationResult::Ignored_EdgeIsFullyConstrained;
	}


	// look up verts and tris for this edge
	if (Mesh->IsEdge(edgeID) == false)
	{
		return ESimplificationResult::Failed_NotAnEdge;
	}
	FIndex4i edgeInfo = Mesh->GetEdge(edgeID);
	int a = edgeInfo.A, b = edgeInfo.B, t0 = edgeInfo.C, t1 = edgeInfo.D;
	bool bIsBoundaryEdge = (t1 == FDynamicMesh3::InvalidID);

	// look up 'other' verts c (from t0) and d (from t1, if it exists)
	FIndex3i T0tv = Mesh->GetTriangle(t0);
	int c = IndexUtil::FindTriOtherVtx(a, b, T0tv);
	FIndex3i T1tv = (bIsBoundaryEdge) ? FDynamicMesh3::InvalidTriangle : Mesh->GetTriangle(t1);
	int d = (bIsBoundaryEdge) ? FDynamicMesh3::InvalidID : IndexUtil::FindTriOtherVtx(a, b, T1tv);

	FVector3d vA = Mesh->GetVertex(a);
	FVector3d vB = Mesh->GetVertex(b);
	double edge_len_sqr = (vA - vB).SquaredLength();
	if (edge_len_sqr > MinEdgeLength * MinEdgeLength)
	{
		return ESimplificationResult::Ignored_EdgeTooLong;
	}

	ProfileBeginCollapse();

	// check if we should collapse, and also find which vertex we should collapse to,
	// in cases where we have constraints/etc
	int collapse_to = -1;
	bool bCanCollapse = CanCollapseEdge(edgeID, a, b, c, d, t0, t1, collapse_to);
	if (bCanCollapse == false)
	{
		return ESimplificationResult::Ignored_Constrained;
	}

	// if we have a boundary, we want to collapse to boundary
	if (bPreserveBoundaryShape && bHaveBoundary)
	{
		if (collapse_to != -1)
		{
			if ((IsBoundaryVertex(b) && collapse_to != b) ||
				(IsBoundaryVertex(a) && collapse_to != a))
			{
				return ESimplificationResult::Ignored_Constrained;
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
	ESimplificationResult retVal = ESimplificationResult::Failed_OpNotSuccessful;

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
		return ESimplificationResult::Ignored_CreatesFlip;
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

		retVal = ESimplificationResult::Ok_Collapsed;
	}

	ProfileEndCollapse();
	return retVal;
}








// Project vertices onto projection target. 
// We can do projection in parallel if we have .net 
template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::FullProjectionPass()
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

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::ApplyToProjectVertices(const TFunction<void(int)>& apply_f)
{
	for (int vid : Mesh->VertexIndicesItr())
	{
		apply_f(vid);
	}
}

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::ProjectVertex(int vID, IProjectionTarget* targetIn)
{
	FVector3d curpos = Mesh->GetVertex(vID);
	FVector3d projected = targetIn->Project(curpos, vID);
	Mesh->SetVertex(vID, projected);
}

// used by collapse-edge to get projected position for new vertex
template <typename QuadricErrorType>
FVector3d TMeshSimplification<QuadricErrorType>::GetProjectedCollapsePosition(int vid, const FVector3d& vNewPos)
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


// Custom behavior for FAttrBasedQuadric simplifier.
template<>
void TMeshSimplification<FAttrBasedQuadricErrord>::OnEdgeCollapse(int edgeID, int va, int vb, const FDynamicMesh3::FEdgeCollapseInfo& collapseInfo)
{

	// Update the normal
	FAttrBasedQuadricErrord& Quadric = EdgeQuadrics[edgeID].q;
	FVector3d collapse_pt = EdgeQuadrics[edgeID].collapse_pt;

	FVector3d UpdatedNormald;
	Quadric.ComputeAttributes(collapse_pt, UpdatedNormald);

	FVector3f UpdatedNormal(UpdatedNormald.X, UpdatedNormald.Y, UpdatedNormald.Z);
	UpdatedNormal.Normalize();

	if (NormalOverlay != nullptr)
	{
		// Get all the elements associated with this vertex (could be more than one to account for split vertex data)
		TArray<int> ElementIdArray;
		NormalOverlay->GetVertexElements(va, ElementIdArray);

		// update everyone with the same normal.
		for (int ElementId : ElementIdArray)
		{
			NormalOverlay->SetElement(ElementId, UpdatedNormal);
		}
	}
	else
	{
		Mesh->SetVertexNormal(va, UpdatedNormal);
	}

}



// These are explicit instantiations of the templates that are exported from the shared lib.
// Only these instantiations of the template can be used.
// This is necessary because we have placed most of the templated functions in this .cpp file, instead of the header.
template class DYNAMICMESH_API TMeshSimplification< FAttrBasedQuadricErrord >;
template class DYNAMICMESH_API TMeshSimplification< FVolPresQuadricErrord >;
template class DYNAMICMESH_API TMeshSimplification< FQuadricErrord >;
