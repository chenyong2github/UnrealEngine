// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "DynamicMeshAttributeSet.h"
#include "Util/IndexUtil.h"
#include "Async/ParallelFor.h"
#include "Templates/UnrealTypeTraits.h"



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

	double attrweight = 16.;
	return FQuadricErrorType(p0, p1, p2, n0d, n1d, n2d, nface, c, attrweight);
}

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::InitializeTriQuadrics()
{
	const int NT = Mesh->MaxTriangleID();
	triQuadrics.SetNum(NT);
	triAreas.SetNum(NT);

	// tested with ParallelFor - no measurable benefit
	//@todo parallel version
	//gParallel.BlockStartEnd(0, Mesh->MaxTriangleID - 1, (start_tid, end_tid) = > {
	FVector3d n, c;
	for (int tid : Mesh->TriangleIndicesItr())
	{
		triQuadrics[tid] = ComputeFaceQuadric(tid, n, c, triAreas[tid]);
	}

}

template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::InitializeSeamQuadrics()
{
	// early out if this feature isn't needed.
	if (!bAllowSeamCollapse)
	{
		return;
	}

	double EdgeWeight = this->SeamEdgeWeight;

	auto AddSeamQuadric = [EdgeWeight, this](int eid)
	{
		FDynamicMesh3::FEdge edge = Mesh->GetEdge(eid);
		FVector3d p0 = Mesh->GetVertex(edge.Vert[0]);
		FVector3d p1 = Mesh->GetVertex(edge.Vert[1]);

		// face normal 
		FVector3d nA = Mesh->GetTriNormal(edge.Tri.A);

		// this constrains the point to a plane aligned with the edge and normal to the face
		FSeamQuadricType& seamQuadric = seamQuadrics.Add(eid, CreateSeamQuadric(p0, p1, nA));


		// add the other side - this constrains the point to the line where the two planes intersect.
		if (edge.Tri.B != FDynamicMesh3::InvalidID)
		{
			FVector3d nB = Mesh->GetTriNormal(edge.Tri.B);
			seamQuadric.Add(CreateSeamQuadric(p0, p1, nB));
		}

		seamQuadric.Scale(EdgeWeight);
	};

	if (Constraints) // The edge constraints an entry for each seam, boundary, group boundary and material boundary
	{
		const auto& EdgeConstraints = Constraints->GetEdgeConstraints();

		for (auto& ConstraintPair : EdgeConstraints)
		{
			int eid = ConstraintPair.Key;

			AddSeamQuadric(eid);
		}

	}
	else
	{
		const FDynamicMeshAttributeSet* Attributes = Mesh->Attributes();

		for (int eid : Mesh->EdgeIndicesItr())
		{
			bool bNeedsQuadric = Mesh->IsBoundaryEdge(eid);
			bNeedsQuadric = bNeedsQuadric || Mesh->IsGroupBoundaryEdge(eid);
			if (Attributes)
			{
				bNeedsQuadric = bNeedsQuadric || Attributes->IsMaterialBoundaryEdge(eid);
				bNeedsQuadric = bNeedsQuadric || Attributes->IsSeamEdge(eid);
			}

			if (bNeedsQuadric)
			{
				AddSeamQuadric(eid);
			}
		}
	}
}


template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::InitializeVertexQuadrics()
{

	int NV = Mesh->MaxVertexID();
	vertQuadrics.SetNum(NV);
	// tested with ParallelFor - no measurable benefit 
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
QuadricErrorType TMeshSimplification<QuadricErrorType>::AssembleEdgeQuadric(const FDynamicMesh3::FEdge& edge) const
{
	//  form standard edge quadric as sum of the vertex quadrics for the edge endpoints
	QuadricErrorType EdgeQuadric(vertQuadrics[edge.Vert.A], vertQuadrics[edge.Vert.B]);
	
	if (!bRetainQuadricMemory)
	{ 
		// the edge.Tri faces are double counted. Remove one.
		const FIndex2i& Tris = edge.Tri;
		if (Tris.A != FDynamicMesh3::InvalidID)
		{
			EdgeQuadric.Add(-triAreas[Tris.A], triQuadrics[Tris.A]);
		}

		if (Tris.B != FDynamicMesh3::InvalidID)
		{
			EdgeQuadric.Add(-triAreas[Tris.B], triQuadrics[Tris.B]);
		}
	}
	if (bAllowSeamCollapse)
	{ 
		// lambda that adds any adjacent seam quadrics to the edge quadric
		auto AddSeamQuadricsToEdge = [&, this](int vid)
		{
			for (int eid : Mesh->VtxEdgesItr(vid))
			{
				if (const FSeamQuadricType* seamQuadric =  seamQuadrics.Find(eid))
				{
					EdgeQuadric.AddSeamQuadric(*seamQuadric);
				} 
			}
		};
	
		// accumulate any adjacent seam quadrics onto this edge quadric.
		AddSeamQuadricsToEdge(edge.Vert.A);
		AddSeamQuadricsToEdge(edge.Vert.B);
	}

	return EdgeQuadric;
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
		FDynamicMesh3::FEdge edge = Mesh->GetEdge(eid);
		FQuadricErrorType Q = AssembleEdgeQuadric(edge);
		FVector3d opt = OptimalPoint(eid, Q, edge.Vert.A, edge.Vert.B);
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
			QEdge& edge = EdgeQuadrics[eid];
			float error = EdgeErrors[i].error;
			EdgeQueue.Insert(eid, error);
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

	switch (CollapseMode)
	{
		case ESimplificationCollapseModes::AverageVertexPosition:
		{
			return GetProjectedPoint((Mesh->GetVertex(ea) + Mesh->GetVertex(eb)) * 0.5);
		}
		break;

		case ESimplificationCollapseModes::MinimalExistingVertexError:
		{
			FVector3d va = Mesh->GetVertex(ea);
			FVector3d vb = Mesh->GetVertex(eb);
			double fa = q.Evaluate(va);
			double fb = q.Evaluate(vb);
			if (fa < fb)
			{
				return va;
			}
			else
			{
				return vb;
			}
		
		}
		break;

		case ESimplificationCollapseModes::MinimalQuadricPositionError:
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
		break;
	default:

		// should never happen
		checkSlow(0);
		return FVector3d::Zero();
	}
}




template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::UpdateNeighborhood(const FDynamicMesh3::FEdgeCollapseInfo& collapseInfo)
{	
	int kvid = collapseInfo.KeptVertex;
	int rvid = collapseInfo.RemovedVertex;

	FIndex2i removedTris = collapseInfo.RemovedTris;
	FIndex2i opposingVerts = collapseInfo.OpposingVerts;

	// --- Update the seam quadrics
	if (bAllowSeamCollapse)
	{
		
		FIndex2i removedEdges = collapseInfo.RemovedEdges;
		FIndex2i keptEdges = collapseInfo.KeptEdges;

		// update the map between edge id and seam quadrics 
		// if constraints exist, they define the edges with seam quadrics
		// otherwise require kept edges to have a seam quadric if either 
		// the kept or collapse edge had a seam quadric.
		if (Constraints)  // quadrics on the constrained edges
		{
			if (Constraints->HasEdgeConstraint(keptEdges.A))
			{
				seamQuadrics.Add(keptEdges.A);
			}
			else
			{
				seamQuadrics.Remove(keptEdges.A);
			}
			
			if (keptEdges.B != FDynamicMesh3::InvalidID)
			{ 
				if( Constraints->HasEdgeConstraint(keptEdges.B))
				{
					seamQuadrics.Add(keptEdges.B);
				}
				else
				{
					seamQuadrics.Remove(keptEdges.B);
				}
			}
		}
		else // propagate any existing seam quadric requirements.
		{			
			if (FSeamQuadricType* seamQuadric = seamQuadrics.Find(removedEdges.A))
			{
				seamQuadrics.Add(keptEdges.A);
			}
			if (removedEdges.B != FDynamicMesh3::InvalidID)
			{
				if (FSeamQuadricType* seamQuadric = seamQuadrics.Find(removedEdges.B))
				{
					seamQuadrics.Add(keptEdges.B);
				}
			}
		}

		// removed quadrics from deleted edges
		seamQuadrics.Remove(removedEdges.A);
		if (removedEdges.B != FDynamicMesh3::InvalidID)
		{
			seamQuadrics.Remove(removedEdges.B);
		}
		
		// update any seam quadrics adjacent to kvid to reflect changes in the seams
	
		double EdgeWeight = this->SeamEdgeWeight;

		for (int eid : Mesh->VtxEdgesItr(kvid))
		{
			FDynamicMesh3::FEdge ne = Mesh->GetEdge(eid);

			// need to recompute this seam quadric
			if (FSeamQuadricType* seamQuadric = seamQuadrics.Find(eid))
			{
				// rebuild the seam quadric

				FVector3d p0 = Mesh->GetVertex(ne.Vert[0]);
				FVector3d p1 = Mesh->GetVertex(ne.Vert[1]);

				// face normal 
				FVector3d nA = Mesh->GetTriNormal(ne.Tri.A);

				// this constrains the point to a plane aligned with the edge and normal to the face
				*seamQuadric = CreateSeamQuadric(p0, p1, nA);
				// add the other side - this constrains the point to the line where the two planes intersect.
				if (ne.Tri.B != FDynamicMesh3::InvalidID)
				{
					FVector3d nB = Mesh->GetTriNormal(ne.Tri.B);
					seamQuadric->Add(CreateSeamQuadric(p0, p1, nB));
				}

				seamQuadric->Scale(EdgeWeight);
			}
		}
	}

	// --- Update the vertex quadrics
	if (bRetainQuadricMemory)
	{ 
		// Quadric "memory"  the retained vertex quadric is the sum of the two vert quadrics
		vertQuadrics[kvid] = QuadricErrorType(vertQuadrics[kvid], vertQuadrics[rvid]);
	}
	else
	{
		// compute the change in affected face quadrics, and then propagate 
			// that change to the face adjacent verts.
		FVector3d n, c;
		double NewtriArea;

		// Update the triangle areas and quadrics that will have changed
		for (int tid : Mesh->VtxTrianglesItr(kvid))
		{

			const double OldtriArea = triAreas[tid];
			const FQuadricErrorType OldtriQuadric = triQuadrics[tid];


			// compute the new quadric for this tri.
			FQuadricErrorType NewtriQuadric = ComputeFaceQuadric(tid, n, c, NewtriArea);

			// update the arrays that hold the current face area & quadric
			triAreas[tid] = NewtriArea;
			triQuadrics[tid] = NewtriQuadric;

			FIndex3i tri_vids = Mesh->GetTriangle(tid);

			// update the vert quadrics that are adjacent to vid.
			for (int32 i = 0; i < 3; ++i)
			{
				if (tri_vids[i] == kvid) continue;

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

					// subtract the quadric from the opposing vert
					vertQuadrics[opposingVerts[i]].Add(-oldArea, oldQuadric);

					// zero out the quadric & area for the removed tris.
					triQuadrics[removedTris[i]] = FQuadricErrorType::Zero();
					triAreas[removedTris[i]] = 0.;
				}
			}
		}
		// Rebuild the quadric for the vert that was retained during the collapse.
		// NB: in the version with memory this quadric took the value of the edge quadric that collapsed.
		{
			FQuadricErrorType vertQuadric = FQuadricErrorType::Zero();
			for (int tid : Mesh->VtxTrianglesItr(kvid))
			{
				vertQuadric.Add(triAreas[tid], triQuadrics[tid]);
			}
			vertQuadrics[kvid] = vertQuadric;
		}
	}

	// --- Update all edge quadrics in the nbrhood
	// NB: this has to follow updating all potential seam quadrics adjacent to kvid 
	// because an edge quadric gathers seam quadrics adjacent the ends 

	if (bRetainQuadricMemory)
	{ 
		for (int eid : Mesh->VtxEdgesItr(kvid))
		{
			FDynamicMesh3::FEdge ne = Mesh->GetEdge(eid);

			QuadricErrorType Q = AssembleEdgeQuadric(ne);
			FVector3d opt = OptimalPoint(eid, Q, ne.Vert.A, ne.Vert.B);
			float err = (float)Q.Evaluate(opt);
			EdgeQuadrics[eid] = QEdge(eid, Q, opt);
			if (EdgeQueue.Contains(eid))
			{
				EdgeQueue.Update(eid, err);
			}
			else
			{
				EdgeQueue.Insert(eid, err);
			}
		}
	}
	else
	{
		TArray<int, TInlineAllocator<64>> EdgesToUpdate;
		for (int adjeid : Mesh->VtxEdgesItr(kvid))
		{
			EdgesToUpdate.Add(adjeid);

			const FIndex2i Verts = Mesh->GetEdgeV(adjeid);
			int adjvid = (Verts[0] == kvid) ? Verts[1] : Verts[0];
			if (adjvid != FDynamicMesh3::InvalidID)
			{
				for (int eid : Mesh->VtxEdgesItr(adjvid))
				{
					if (eid != adjeid)
					{
						EdgesToUpdate.AddUnique(eid);
					}
				}
			}
		}

		for (int eid : EdgesToUpdate)
		{
		
			const FDynamicMesh3::FEdge edgeData = Mesh->GetEdge(eid);
			FQuadricErrorType Q = AssembleEdgeQuadric(edgeData);

			FVector3d opt = OptimalPoint(eid, Q, edgeData.Vert[0], edgeData.Vert[1]);
			float err = (float)Q.Evaluate(opt);
			EdgeQuadrics[eid] = QEdge(eid, Q, opt);
			if (EdgeQueue.Contains(eid))
			{
				EdgeQueue.Update(eid, err);
			}
			else
			{
				EdgeQueue.Insert(eid, err);
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

	if (Mesh->HasAttributes() && GetConstraints().IsSet() == false)
	{
		ensureMsgf(false, TEXT("Input Mesh has Attribute overlays but no Constraints are configured. Use FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams() to create a Constraint Set for Attribute seams."));
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
	InitializeSeamQuadrics();
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


		FDynamicMesh3::FEdgeCollapseInfo collapseInfo;
		ESimplificationResult result = CollapseEdge(eid, EdgeQuadrics[eid].collapse_pt, collapseInfo);
		if (result == ESimplificationResult::Ok_Collapsed)
		{
			// update the quadrics
			UpdateNeighborhood(collapseInfo);

		}
		else if (result == ESimplificationResult::Failed_IsolatedTriangle && Mesh->TriangleCount() > 2)
		{
			const FDynamicMesh3::FEdge Edge = Mesh->GetEdge(eid);
			RemoveIsolatedTriangle(Edge.Tri.A);
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



template<typename GetTriNormalFuncType>
static bool IsDevelopableVertex(const FDynamicMesh3& Mesh, int32 VertexID, double DotTolerance,
	GetTriNormalFuncType GetTriNormalFunc)
{
	FVector3d Normal1, Normal2;
	int32 Normal1Count = 0, Normal2Count = 0, OtherCount = 0;
	Mesh.EnumerateVertexTriangles(VertexID, [&](int32 tid)
	{
		FVector3d TriNormal = GetTriNormalFunc(tid);
		if (Normal1Count == 0)
		{
			Normal1 = TriNormal;
			Normal1Count++;
			return;
		}
		if (TriNormal.Dot(Normal1) > DotTolerance)
		{
			Normal1Count++;
			return;
		}
		if (Normal2Count == 0)
		{
			Normal2 = TriNormal;
			Normal2Count++;
			return;
		}
		if (TriNormal.Dot(Normal2) > DotTolerance)
		{
			Normal2Count++;
			return;
		}
		OtherCount++;
	});
	return OtherCount == 0;
}




template<typename GetTriNormalFuncType>
static bool IsCollapsableDevelopableEdge(const FDynamicMesh3& Mesh, int32 CollapseEdgeID, int32 RemoveV, int32 KeepV, double DotTolerance,
	GetTriNormalFuncType GetTriNormalFunc)
{
	FIndex2i CollapseEdgeT = Mesh.GetEdgeT(CollapseEdgeID);
	FVector3d Normal1 = GetTriNormalFunc(CollapseEdgeT.A);
	FVector3d Normal2 = GetTriNormalFunc(CollapseEdgeT.B);

	// assuming is that RemoveV is developable vertex...should check?

	// planar case
	if (Normal1.Dot(Normal2) > DotTolerance)
	{
		bool bIsFlat = true;
		Mesh.EnumerateVertexTriangles(RemoveV, [&](int32 tid)
		{
			if (GetTriNormalFunc(tid).Dot(Normal1) < DotTolerance)
			{
				bIsFlat = false;
			}
		});
		return bIsFlat;
	}

	// if we are not planar, we need to find the 'other' developable edge at RemoveV.
	// This edge must be aligned w/ our collapse edge and have the same normals
	FVector3d A = Mesh.GetVertex(RemoveV), B = Mesh.GetVertex(KeepV);
	FVector3d EdgeDir(B - A); EdgeDir.Normalize();
	int32 FoldEdges = 0, FlatEdges = 0, OtherEdges = 0;
	for (int32 eid : Mesh.VtxEdgesItr(RemoveV))
	{
		if (eid != CollapseEdgeID)
		{
			FIndex2i EdgeT = Mesh.GetEdgeT(eid);
			if (EdgeT.B == IndexConstants::InvalidID)
			{
				return false;		// abort if one of the edges of RemoveV is a boundary edge (?)
			}
			FVector3d Normal3 = GetTriNormalFunc(EdgeT.A);
			FVector3d Normal4 = GetTriNormalFunc(EdgeT.B);

			FIndex2i OtherEdgeV = Mesh.GetEdgeV(eid);
			int32 OtherV = IndexUtil::FindEdgeOtherVertex(OtherEdgeV, RemoveV);
			FVector3d C = Mesh.GetVertex(OtherV);
			if ((A-C).Normalized().Dot(EdgeDir) > DotTolerance)
			{
				if ((Normal3.Dot(Normal1) > DotTolerance && Normal4.Dot(Normal2) > DotTolerance) ||
					(Normal3.Dot(Normal2) > DotTolerance && Normal4.Dot(Normal1) > DotTolerance))
				{
					FoldEdges++;
				}
			}
			else if ( Normal3.Dot(Normal4) > DotTolerance)
			{
				FlatEdges++;
			}
			else
			{
				OtherEdges++;
			}
		}
	}
	return (FoldEdges == 1 && OtherEdges == 0);
}


template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::SimplifyToMinimalPlanar(
	double CoplanarAngleTolDeg,
	TFunctionRef<bool(int32 EdgeID)> EdgeFilterPredicate)
{
#define RETURN_IF_CANCELLED 	if (Cancelled()) { return; }

	if (Mesh->TriangleCount() == 0)    // badness if we don't catch this...
	{
		return;
	}

	// we don't collapse on the boundary
	bHaveBoundary = false;

	// keep triangle normals
	TArray<FVector3d> TriNormals;
	TArray<bool> DevelopableVerts;

	ProfileBeginPass();

	ProfileBeginSetup();
	Precompute();
	RETURN_IF_CANCELLED;

	TriNormals.SetNum(Mesh->MaxTriangleID());
	ParallelFor(Mesh->MaxTriangleID(), [&](int32 tid)
	{
		if (Mesh->IsTriangle(tid))
		{
			TriNormals[tid] = Mesh->GetTriNormal(tid);
		}
	});
	RETURN_IF_CANCELLED;

	DevelopableVerts.SetNum(Mesh->MaxVertexID());
	double PlanarDotTol = FMathd::Cos( CoplanarAngleTolDeg * FMathd::DegToRad );
	ParallelFor(Mesh->MaxVertexID(), [&](int32 vid)
	{
		if (Mesh->IsVertex(vid))
		{
			DevelopableVerts[vid] = IsDevelopableVertex(*Mesh, vid, PlanarDotTol, [&](int32 tid) { return TriNormals[tid]; });
		}
	});
	RETURN_IF_CANCELLED;

	ProfileEndSetup();


	ProfileBeginOps();

	ProfileBeginCollapse();

	TArray<int32> CollapseEdges;
	int32 MaxRounds = 50;
	int32 num_last_pass = 0;
	for (int ri = 0; ri < MaxRounds; ++ri)
	{
		num_last_pass = 0;

		// collect up edges we have identified for collapse
		CollapseEdges.Reset();
		for (int32 eid : Mesh->EdgeIndicesItr())
		{
			if (EdgeFilterPredicate(eid) == false)
			{
				continue;
			}

			FIndex2i ev = Mesh->GetEdgeV(eid);
			if (DevelopableVerts[ev.A] || DevelopableVerts[ev.B])
			{
				CollapseEdges.Add(eid);
			}
		}


		FVector3d va = FVector3d::Zero(), vb = FVector3d::Zero();
		for ( int32 eid : CollapseEdges )
		{
			if ( (Mesh->IsEdge(eid) == false) || Mesh->IsBoundaryEdge(eid) )
			{
				continue;
			}
			RETURN_IF_CANCELLED;
			COUNT_ITERATIONS++;

			FIndex2i ev = Mesh->GetEdgeV(eid);
			bool bDevelopableA = DevelopableVerts[ev.A];
			bool bDevelopableB = DevelopableVerts[ev.B];
			if (bDevelopableA || bDevelopableB)		// this may change during execution as edges are collapsed
			{
				if (! bDevelopableA)		// any other preference for verts?
				{
					Swap(ev.A, ev.B);
				}

				bool bIsCollapsible = IsCollapsableDevelopableEdge(*Mesh, eid, ev.A, ev.B, PlanarDotTol, [&](int32 tid) { return TriNormals[tid]; });
				if (bIsCollapsible)
				{
					FDynamicMesh3::FEdgeCollapseInfo collapseInfo;
					ESimplificationResult result;
					result = CollapseEdge(eid, Mesh->GetVertex(ev.B), collapseInfo, ev.B);
					if (result == ESimplificationResult::Ok_Collapsed)
					{
						++num_last_pass;

						int vKeptID = collapseInfo.KeptVertex;
						Mesh->EnumerateVertexTriangles(vKeptID, [&](int32 tid)
						{
							TriNormals[tid] = Mesh->GetTriNormal(tid);
						});
						for (int32 vid : Mesh->VtxVerticesItr(vKeptID))
						{
							DevelopableVerts[vid] = IsDevelopableVertex(*Mesh, vid, PlanarDotTol, [&](int32 tid) { return TriNormals[tid]; });
						}
						DevelopableVerts[vKeptID] = IsDevelopableVertex(*Mesh, vKeptID, PlanarDotTol, [&](int32 tid) { return TriNormals[tid]; });
					}
					else if (bDevelopableA && bDevelopableB &&
								IsCollapsableDevelopableEdge(*Mesh, eid, ev.B, ev.A, PlanarDotTol, [&](int32 tid) { return TriNormals[tid]; }) )
					{
						// we can try collapsing to A
						result = CollapseEdge(eid, Mesh->GetVertex(ev.A), collapseInfo, ev.A);
						if (result == ESimplificationResult::Ok_Collapsed)
						{
							++num_last_pass;

							int vKeptID = collapseInfo.KeptVertex;
							Mesh->EnumerateVertexTriangles(vKeptID, [&](int32 tid)
							{
								TriNormals[tid] = Mesh->GetTriNormal(tid);
							});
							for (int32 vid : Mesh->VtxVerticesItr(vKeptID))
							{
								DevelopableVerts[vid] = IsDevelopableVertex(*Mesh, vid, PlanarDotTol, [&](int32 tid) { return TriNormals[tid]; });
							}
							DevelopableVerts[vKeptID] = IsDevelopableVertex(*Mesh, vKeptID, PlanarDotTol, [&](int32 tid) { return TriNormals[tid]; });
						}
					}
				}
			}
		}

		if (num_last_pass == 0)     // converged
		{
			break;
		}
	}
	ProfileEndCollapse();
	ProfileEndOps();

	RETURN_IF_CANCELLED;

	Reproject();

	ProfileEndPass();

#undef RETURN_IF_CANCELLED
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
			FDynamicMesh3::FEdgeCollapseInfo collapseInfo;
			ESimplificationResult result = CollapseEdge(eid, midpoint, collapseInfo);
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
ESimplificationResult TMeshSimplification<QuadricErrorType>::CollapseEdge(int edgeID, FVector3d vNewPos, FDynamicMesh3::FEdgeCollapseInfo& collapseInfo, int32 RequireKeepVert)
{
	collapseInfo.KeptVertex = FDynamicMesh3::InvalidID;
	RuntimeDebugCheck(edgeID);

	FEdgeConstraint constraint =
		(!Constraints) ? FEdgeConstraint::Unconstrained() : Constraints->GetEdgeConstraint(edgeID);
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
	const FDynamicMesh3::FEdge Edge = Mesh->GetEdge(edgeID);
	int a = Edge.Vert[0], b = Edge.Vert[1], t0 = Edge.Tri[0], t1 = Edge.Tri[1];
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

	// check if we should collapse, and also find which vertex we should retain
	// in cases where we have constraints/etc
	int collapse_to = -1;
	bool bCanCollapse = false;
	if (!bAllowSeamCollapse)
	{
		bCanCollapse = CanCollapseEdge(edgeID, a, b, c, d, t0, t1, collapse_to);
	}
	else
	{
		bCanCollapse = CanCollapseVertex(edgeID, a, b, collapse_to);
	}

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

	if (RequireKeepVert == a || RequireKeepVert == b)
	{
		if (collapse_to >= 0 && collapse_to != RequireKeepVert)
		{
			return ESimplificationResult::Ignored_Constrained;
		}
		collapse_to = RequireKeepVert;
	}

	// optimization: if edge cd exists, we cannot collapse or flip. look that up here?
	//  funcs will do it internally...
	//  (or maybe we can collapse if cd exists? edge-collapse doesn't check for it explicitly...)
	ESimplificationResult retVal = ESimplificationResult::Failed_OpNotSuccessful;

	int iKeep = b, iCollapse = a;
	bool bCanMove = true;
	
	if (collapse_to != -1)
	{
		iKeep = collapse_to;
		iCollapse = (iKeep == a) ? b : a;

		// if constraints require a fixed position
		if (Constraints)
		{
			bCanMove = Constraints->GetVertexConstraint(collapse_to).bCanMove;
		}
	}
	double collapse_t = 0;
	if (bCanMove)
	{
		vNewPos = GetProjectedCollapsePosition(iKeep, vNewPos);
		double div = vA.Distance(vB);
		collapse_t = (div < FMathd::ZeroTolerance) ? 0.5 : (vNewPos.Distance(Mesh->GetVertex(iKeep))) / div;
		collapse_t = VectorUtil::Clamp(collapse_t, 0.0, 1.0);
	}
	else
	{
		vNewPos = (collapse_to == a) ? vA : vB;
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

	EMeshResult result = Mesh->CollapseEdge(iKeep, iCollapse, collapse_t, collapseInfo);
	if (result == EMeshResult::Ok)
	{
		Mesh->SetVertex(iKeep, vNewPos);
		if (Constraints)
		{
			Constraints->ClearEdgeConstraint(edgeID);

			auto ConstraintUpdator = [this](int cur_eid)->void
			{
				
				// Seam edge can never flip, it is never fully unconstrained 
				EEdgeRefineFlags SeamEdgeConstraint = EEdgeRefineFlags::NoFlip;
				if (!bAllowSeamCollapse)
				{
					SeamEdgeConstraint = EEdgeRefineFlags((int)SeamEdgeConstraint | (int)EEdgeRefineFlags::NoCollapse);
				}

				FEdgeConstraint UpdatedEdgeConstraint;
				FVertexConstraint UpdatedVertexConstraintA;
				FVertexConstraint UpdatedVertexConstraintB;


				bool bHaveUpdate = 
				FMeshConstraintsUtil::ConstrainEdgeBoundariesAndSeams(cur_eid,
					*Mesh,
					MeshBoundaryConstraint,
					GroupBoundaryConstraint,
					MaterialBoundaryConstraint,
					SeamEdgeConstraint,
					!bAllowSeamCollapse,
					UpdatedEdgeConstraint,
					UpdatedVertexConstraintA,
					UpdatedVertexConstraintB);

				if (bHaveUpdate)
				{
					FIndex2i EdgeVerts = Mesh->GetEdgeV(cur_eid);

					Constraints->SetOrUpdateEdgeConstraint(cur_eid, UpdatedEdgeConstraint);
					UpdatedVertexConstraintA.CombineConstraint(Constraints->GetVertexConstraint(EdgeVerts.A));
					Constraints->SetOrUpdateVertexConstraint(EdgeVerts.A, UpdatedVertexConstraintA);

					UpdatedVertexConstraintB.CombineConstraint(Constraints->GetVertexConstraint(EdgeVerts.B));
					Constraints->SetOrUpdateVertexConstraint(EdgeVerts.B, UpdatedVertexConstraintB);
				}
			};
			
			if (Constraints->HasEdgeConstraint(collapseInfo.RemovedEdges.A))
			{

				Constraints->ClearEdgeConstraint(collapseInfo.KeptEdges.A);
				Constraints->ClearEdgeConstraint(collapseInfo.RemovedEdges.A);

				ConstraintUpdator(collapseInfo.KeptEdges.A);
			}
			
			if (collapseInfo.RemovedEdges.B != FDynamicMesh3::InvalidID)
			{
				if (Constraints->HasEdgeConstraint(collapseInfo.RemovedEdges.B))
				{

					Constraints->ClearEdgeConstraint(collapseInfo.KeptEdges.B);
					Constraints->ClearEdgeConstraint(collapseInfo.RemovedEdges.B);

					ConstraintUpdator(collapseInfo.KeptEdges.B);
				}
			}
			Constraints->ClearVertexConstraint(iCollapse);
		}
		OnEdgeCollapse(edgeID, iKeep, iCollapse, collapseInfo);
		DoDebugChecks();

		retVal = ESimplificationResult::Ok_Collapsed;
	}
	else if (result == EMeshResult::Failed_CollapseTriangle)
	{
		retVal = ESimplificationResult::Failed_IsolatedTriangle;
	}

	ProfileEndCollapse();
	return retVal;
}


template <typename QuadricErrorType>
bool TMeshSimplification<QuadricErrorType>::RemoveIsolatedTriangle(int tID)
{
	if (!Mesh->IsTriangle(tID)) return true;

	FIndex3i tv = Mesh->GetTriangle(tID);

	bool bIsIsolated = true;
	for (int i = 0; i < 3; ++i)
	{
		for (int nbtr : Mesh->VtxTrianglesItr(tv[i]))
		{
			bIsIsolated = bIsIsolated && (nbtr == tID);
		}
	}


	if (bIsIsolated)
	{
		const FIndex3i TriEdges = Mesh->GetTriEdges(tID);
		if (Mesh->RemoveTriangle(tID) == EMeshResult::Ok)
		{
			if (Constraints)
			{
				Constraints->ClearEdgeConstraint(TriEdges.A);
				Constraints->ClearEdgeConstraint(TriEdges.B);
				Constraints->ClearEdgeConstraint(TriEdges.C);

				Constraints->ClearVertexConstraint(tv.A);
				Constraints->ClearVertexConstraint(tv.B);
				Constraints->ClearVertexConstraint(tv.C);
			}
		}

		OnRemoveIsolatedTriangle(tID);
	}

	return bIsIsolated;

}



// Project vertices onto projection target. 
// We can do projection in parallel if we have .net 
template <typename QuadricErrorType>
void TMeshSimplification<QuadricErrorType>::FullProjectionPass()
{
	auto project = [&](int vID)
	{
		if (IsVertexPositionConstrained(vID))
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

	// TODO: optionally do projection in parallel?
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
	if (Constraints)
	{
		FVertexConstraint vc = Constraints->GetVertexConstraint(vid);
		if (vc.Target != nullptr)
		{
			return vc.Target->Project(vNewPos, vid);
		}
		if (vc.bCanMove == false)
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

		if (ElementIdArray.Num() > 1)
		{
			// keep whatever split normals are currently in the overlay.
			// @todo: normalize the split normals - since the values here result from a lerp
			return;
		}
	
		// at most one element
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
