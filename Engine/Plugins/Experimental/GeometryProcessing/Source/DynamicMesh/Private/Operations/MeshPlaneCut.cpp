// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Operations/MeshPlaneCut.h"
#include "Operations/SimpleHoleFiller.h"
#include "Operations/PlanarHoleFiller.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "MathUtil.h"

#include "Async/ParallelFor.h"

bool FMeshPlaneCut::Cut()
{
	double InvalidDist = -FMathd::MaxReal;

	// TODO: handle selections
	//MeshEdgeSelection CutEdgeSet = null;
	//MeshVertexSelection CutVertexSet = null;
	//if (CutFaceSet != null) {
	//	CutEdgeSet = new MeshEdgeSelection(Mesh, CutFaceSet);
	//	CutVertexSet = new MeshVertexSelection(Mesh, CutEdgeSet);
	//}

	// compute Signs
	int MaxVID = Mesh->MaxVertexID();
	TArray<double> Signs;
	Signs.SetNum(MaxVID);

	bool bNoParallel = false;
	ParallelFor(MaxVID, [&](int32 VID)
	{
		if (Mesh->IsVertex(VID))
		{
			Signs[VID] = (Mesh->GetVertex(VID) - PlaneOrigin).Dot(PlaneNormal);
		}
		else
		{
			Signs[VID] = InvalidDist;
		}
	},
	bNoParallel);

	TSet<int> ZeroEdges, ZeroVertices, OnCutEdges;

	// have to skip processing of new edges. If edge id
	// is > max at start, is new. Otherwise if in NewEdges list, also new.
	int MaxEID = Mesh->MaxEdgeID();
	TSet<int> NewEdges;

	FDynamicMesh3::edge_iterator EdgeItr = Mesh->EdgeIndicesItr();
	// TODO: selection logic
	//IEnumerable<int> edgeItr = Interval1i.Range(MaxEID);
	//if (CutEdgeSet != null)
	//	edgeItr = CutEdgeSet;

	// cut existing edges with plane, using edge split
	for (int EID : EdgeItr)
	{
		if (!Mesh->IsEdge(EID))
		{
			continue;
		}
		if (EID >= MaxEID || NewEdges.Contains(EID))
		{
			continue;
		}

		FIndex2i ev = Mesh->GetEdgeV(EID);
		double f0 = Signs[ev.A];
		double f1 = Signs[ev.B];

		// If both Signs are 0, this edge is on-contour
		// If one sign is 0, that vertex is on-contour
		int n0 = (FMathd::Abs(f0) < FMathd::Epsilon) ? 1 : 0;
		int n1 = (FMathd::Abs(f1) < FMathd::Epsilon) ? 1 : 0;
		if (n0 + n1 > 0)
		{
			if (n0 + n1 == 2)
			{
				ZeroEdges.Add(EID);
			}
			else
			{
				ZeroVertices.Add((n0 == 1) ? ev[0] : ev[1]);
			}
			continue;
		}

		// no crossing
		if (f0 * f1 > 0)
		{
			continue;
		}

		FDynamicMesh3::FEdgeSplitInfo splitInfo;
		double t = f0 / (f0 - f1);
		EMeshResult result = Mesh->SplitEdge(EID, splitInfo, t);
		if (!ensureMsgf(result == EMeshResult::Ok, TEXT("FMeshPlaneCut::Cut: failed to SplitEdge")))
		{
			continue; // edge split really shouldn't fail; skip the edge if it somehow does
		}

		NewEdges.Add(splitInfo.NewEdges.A);
		NewEdges.Add(splitInfo.NewEdges.B); OnCutEdges.Add(splitInfo.NewEdges.B);
		if (splitInfo.NewEdges.C != FDynamicMesh3::InvalidID)
		{
			NewEdges.Add(splitInfo.NewEdges.C); OnCutEdges.Add(splitInfo.NewEdges.C);
		}
	}

	// remove one-rings of all positive-side vertices. 
	// @todo handle selection logic
	//IEnumerable<int> vertexSet = Interval1i.Range(MaxVID);
	//if (CutVertexSet != null)
	//	vertexSet = CutVertexSet;
	for (int VID : Mesh->VertexIndicesItr())
	{ 
		if (VID < Signs.Num() && Signs[VID] > FMathd::Epsilon)
		{
			Mesh->RemoveVertex(VID, true, false);
		}
	}

	// collapse degenerate edges if we got em
	if (bCollapseDegenerateEdgesOnCut)
	{
		CollapseDegenerateEdges(OnCutEdges, ZeroEdges);
	}


	// ok now we extract boundary loops, but restricted
	// to either the zero-edges we found, or the edges we created! bang!!
	
	FMeshBoundaryLoops Loops(Mesh, false);
	Loops.EdgeFilterFunc = [&OnCutEdges, &ZeroEdges](int EID)
	{
		return OnCutEdges.Contains(EID) || ZeroEdges.Contains(EID);
	};
	bool bFoundLoops = Loops.Compute();

	if (bFoundLoops)
	{
		CutLoops = Loops.Loops;
		CutSpans = Loops.Spans;
		CutLoopsFailed = false;
		FoundOpenSpans = CutSpans.Num() > 0;
	}
	else
	{
		CutLoops.Empty();
		CutLoopsFailed = true;
	}

	return !CutLoopsFailed;
}



void FMeshPlaneCut::CollapseDegenerateEdges(const TSet<int>& OnCutEdges, const TSet<int>& ZeroEdges)
{
	TSet<int> Sets[2] { OnCutEdges, ZeroEdges };

	double Tol2 = DegenerateEdgeTol * DegenerateEdgeTol;
	FVector3d A, B;
	int Collapsed = 0;
	do
	{
		Collapsed = 0;
		for (int SetIdx = 0; SetIdx < 2; SetIdx++)
		{
			for (int EID : Sets[SetIdx])
			{
				if (!Mesh->IsEdge(EID))
				{
					continue;
				}
				Mesh->GetEdgeV(EID, A, B);
				if (A.DistanceSquared(B) > Tol2)
				{
					continue;
				}

				FIndex2i EV = Mesh->GetEdgeV(EID);
				// if the vertex we'd remove is on a seam, try removing the other one instead
				if (Mesh->HasAttributes() && Mesh->Attributes()->IsSeamVertex(EV.B, false))
				{
					Swap(EV.A, EV.B);
					// if they were both on seams, then collapse should not happen?  (& would break OnCollapseEdge assumptions in overlay)
					if (Mesh->HasAttributes() && Mesh->Attributes()->IsSeamVertex(EV.B, false))
					{
						continue;
					}
				}
				FDynamicMesh3::FEdgeCollapseInfo CollapseInfo;
				EMeshResult Result = Mesh->CollapseEdge(EV.A, EV.B, CollapseInfo);
				if (Result == EMeshResult::Ok)
				{
					Collapsed++;
				}
			}
		}
	} while (Collapsed != 0);
}


bool FMeshPlaneCut::SimpleHoleFill(int ConstantGroupID)
{
	bool bAllOk = true;

	HoleFillTriangles.Empty();

	FFrame3d ProjectionFrame(PlaneOrigin, PlaneNormal);

	for (const FEdgeLoop& Loop : CutLoops)
	{
		FSimpleHoleFiller Filler(Mesh, Loop);
		int GID = ConstantGroupID >= 0 ? ConstantGroupID : Mesh->AllocateTriangleGroup();
		bAllOk = Filler.Fill(GID) && bAllOk;
		
		HoleFillTriangles.Add(Filler.NewTriangles);

		if (Mesh->HasAttributes())
		{
			FDynamicMeshEditor Editor(Mesh);
			Editor.SetTriangleNormals(Filler.NewTriangles, (FVector3f)PlaneNormal);
			Editor.SetTriangleUVsFromProjection(Filler.NewTriangles, ProjectionFrame, UVScaleFactor);
		}
	}

	return bAllOk;
}


bool FMeshPlaneCut::HoleFill(TFunction<TArray<FIndex3i>(const FGeneralPolygon2d&)> PlanarTriangulationFunc, bool bFillSpans, int ConstantGroupID)
{
	HoleFillTriangles.Empty();

	TArray<TArray<int>> LoopVertices;
	for (const FEdgeLoop& Loop : CutLoops)
	{
		LoopVertices.Add(Loop.Vertices);
	}
	if (bFillSpans)
	{
		for (const FEdgeSpan& Span : CutSpans)
		{
			LoopVertices.Add(Span.Vertices);
		}
	}
	FPlanarHoleFiller Filler(Mesh, &LoopVertices, PlanarTriangulationFunc, PlaneOrigin, PlaneNormal);

	int GID = ConstantGroupID >= 0 ? ConstantGroupID : Mesh->AllocateTriangleGroup();
	bool bFullyFilledHole = Filler.Fill(GID);
	
	HoleFillTriangles.Add(Filler.NewTriangles);
	if (Mesh->HasAttributes())
	{
		FDynamicMeshEditor Editor(Mesh);
		Editor.SetTriangleNormals(Filler.NewTriangles, (FVector3f)PlaneNormal);

		FFrame3d ProjectionFrame(PlaneOrigin, PlaneNormal);
		Editor.SetTriangleUVsFromProjection(Filler.NewTriangles, ProjectionFrame, UVScaleFactor);
	}

	return bFullyFilledHole;
}
